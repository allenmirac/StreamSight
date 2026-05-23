#include "control/PipelineRunner.h"
#include "observe/LatencyTracer.h"

#include "ai/CameraSource.h"
#include "ai/EventLogger.h"
#include "ai/FaceDatabase.h"
#include "ai/FaceDetector.h"
#include "ai/FaceRecognizer.h"
#include "ai/FileSource.h"
#include "ai/FrameAnalyzer.h"
#include "ai/FrameOverlay.h"
#include "ai/H264Encoder.h"
#include "ai/RtspPullSource.h"
#include "ai/VideoSource.h"

#include "ffmpeg/PipelineManager.h"
#include "ffmpeg/RtspOutputAdapter.h"

#include "xop/H264Source.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <opencv2/opencv.hpp>

namespace control {

ffmpeg::PipelineManager* PipelineRunner::s_pipeline_mgr_ = nullptr;

PipelineRunner::PipelineRunner(const StreamTask& task,
                               xop::RtspServer* rtsp_server,
                               observe::MetricsRegistry* metrics,
                               const std::string& node_id)
    : task_(task),
      rtsp_server_(rtsp_server),
      metrics_(metrics),
      node_id_(node_id) {
}

void PipelineRunner::SetPipelineManager(ffmpeg::PipelineManager* mgr) {
	s_pipeline_mgr_ = mgr;
}

PipelineResult PipelineRunner::Run(std::atomic<bool>& stop_flag) {
	if (task_.use_ffmpeg_pipeline && s_pipeline_mgr_) {
		return RunFFmpeg(stop_flag);
	}
	return RunLegacy(stop_flag);
}

// ─── FFmpeg Pipeline Path (new, parallel 3-stage) ───────────────────────

PipelineResult PipelineRunner::RunFFmpeg(std::atomic<bool>& stop_flag) {
	// Build pipeline config from StreamTask
	ffmpeg::PipelineConfig cfg;
	cfg.input_url     = task_.source_uri;
	cfg.fps           = task_.fps;
	cfg.bitrate       = task_.target_bitrate_kbps * 1000;
	cfg.output_width  = task_.width;
	cfg.output_height = task_.height;
	cfg.decode_ring_size  = 4;
	cfg.process_ring_size = 4;
	cfg.enable_backpressure = true;

	// Create RTSP session and output adapter
	xop::MediaSession* session = xop::MediaSession::CreateNew(task_.session_suffix);
	session->AddSource(xop::channel_0, xop::H264Source::CreateNew(task_.fps));
	auto session_id = rtsp_server_->AddSession(session);

	auto rtsp_out = std::make_shared<ffmpeg::RtspOutputAdapter>(
		rtsp_server_, session_id, 0);
	cfg.outputs.push_back(rtsp_out);

	// Set up AI callback (same as legacy path)
	std::unique_ptr<ai::FaceDetector>   detector;
	std::unique_ptr<ai::FaceRecognizer> recognizer;
	std::unique_ptr<ai::FaceDatabase>   database;
	std::unique_ptr<ai::FrameAnalyzer>  analyzer;
	std::unique_ptr<ai::FrameOverlay>   overlay;
	std::unique_ptr<ai::EventLogger>    logger;

	bool ai_enabled = task_.enable_ai;
	if (ai_enabled) {
		detector.reset(new ai::FaceDetector(task_.detect_model));
		recognizer.reset(new ai::FaceRecognizer(task_.recog_model));
		database.reset(new ai::FaceDatabase(task_.face_db_path));

		if (!detector->Load()) ai_enabled = false;
		if (!recognizer->Load()) ai_enabled = false;
		database->Load();

		if (ai_enabled) {
			analyzer.reset(new ai::FrameAnalyzer(detector.get(), recognizer.get(), database.get()));
			analyzer->SetAnalyzeRate(task_.analyze_fps);

			if (task_.enable_overlay) {
				overlay.reset(new ai::FrameOverlay());
			}

			logger.reset(new ai::EventLogger(task_.log_path));
			logger->Open();
			analyzer->SetEventCallback([&](const ai::AnalysisResult& r) {
				if (logger) logger->Log(r);
			});

			// Capture raw pointers for the callback lambda
			auto* analyzer_ptr = analyzer.get();
			auto* overlay_ptr  = overlay.get();

			cfg.frame_cb = [analyzer_ptr, overlay_ptr](ffmpeg::FFmpegFrame& ff_frame) -> bool {
				cv::Mat frame = ffmpeg::FrameToMat(ff_frame);
				ai::AnalysisResult result = analyzer_ptr->Analyze(frame);
				if (overlay_ptr) overlay_ptr->Draw(frame, result);
				return true;
			};
		}
	}

	// Start pipeline via PipelineManager
	if (!s_pipeline_mgr_->AddStream(task_.stream_id, cfg)) {
		rtsp_server_->RemoveSession(session_id);
		return { PipelineExitCode::Failed, "PipelineManager AddStream failed" };
	}

	std::cout << "[Pipeline] stream=" << task_.stream_id
	          << " node=" << node_id_
	          << " mode=ffmpeg-parallel"
	          << " rtsp://localhost:" << task_.rtsp_port
	          << "/" << task_.session_suffix << std::endl;

	// Wait for stop or pipeline exit
	while (!stop_flag.load()) {
		auto stats = s_pipeline_mgr_->GetStats(task_.stream_id);
		if (!stats.running) {
			break;
		}

		if (metrics_) {
			metrics_->SetGauge("stream." + task_.stream_id, "ring_decode_fill",
			                   (double)stats.decode_ring_fill);
			metrics_->SetGauge("stream." + task_.stream_id, "ring_process_fill",
			                   (double)stats.process_ring_fill);
			metrics_->IncCounter("stream." + task_.stream_id, "frames_decoded",
			                     (double)stats.frames_decoded);
			metrics_->IncCounter("stream." + task_.stream_id, "frames_dropped_demux",
			                     (double)stats.frames_dropped_demux);
			metrics_->IncCounter("stream." + task_.stream_id, "frames_dropped_ai",
			                     (double)stats.frames_dropped_ai);
			metrics_->IncCounter("stream." + task_.stream_id, "frames_pruned_demux",
			                     (double)stats.frames_pruned_demux);
			metrics_->IncCounter("stream." + task_.stream_id, "frames_pruned_ai",
			                     (double)stats.frames_pruned_ai);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

	s_pipeline_mgr_->RemoveStream(task_.stream_id);
	rtsp_server_->RemoveSession(session_id);

	if (stop_flag.load()) {
		return { PipelineExitCode::Stopped, "stop requested" };
	}
	return { PipelineExitCode::Completed, "pipeline exited" };
}

// ─── Legacy Pipeline Path (original, unchanged) ─────────────────────────

PipelineResult PipelineRunner::RunLegacy(std::atomic<bool>& stop_flag) {
	using steady_clock = std::chrono::steady_clock;

	std::unique_ptr<ai::VideoSource> source;
	if (task_.source_type == SourceType::Camera) {
		source.reset(new ai::CameraSource(task_.camera_device, task_.width, task_.height, task_.fps));
	} else if (task_.source_type == SourceType::Rtsp) {
		source.reset(new ai::RtspPullSource(task_.source_uri));
	} else {
		source.reset(new ai::FileSource(task_.source_uri, task_.loop_input));
	}

	if (!source || !source->Open()) {
		return { PipelineExitCode::Failed, "open video source failed" };
	}

	std::unique_ptr<ai::FaceDetector>   detector;
	std::unique_ptr<ai::FaceRecognizer> recognizer;
	std::unique_ptr<ai::FaceDatabase>   database;
	std::unique_ptr<ai::FrameAnalyzer>  analyzer;
	std::unique_ptr<ai::FrameOverlay>   overlay;
	std::unique_ptr<ai::EventLogger>    logger;

	bool ai_enabled = task_.enable_ai;
	if (ai_enabled) {
		detector.reset(new ai::FaceDetector(task_.detect_model));
		recognizer.reset(new ai::FaceRecognizer(task_.recog_model));
		database.reset(new ai::FaceDatabase(task_.face_db_path));

		if (!detector->Load()) ai_enabled = false;
		if (!recognizer->Load()) ai_enabled = false;
		database->Load();

		if (ai_enabled) {
			analyzer.reset(new ai::FrameAnalyzer(detector.get(), recognizer.get(), database.get()));
			analyzer->SetAnalyzeRate(task_.analyze_fps);

			if (task_.enable_overlay) {
				overlay.reset(new ai::FrameOverlay());
			}

			logger.reset(new ai::EventLogger(task_.log_path));
			logger->Open();
			analyzer->SetEventCallback([&](const ai::AnalysisResult& r) {
				if (logger) logger->Log(r);
			});
		}
	}

	xop::MediaSession* session = xop::MediaSession::CreateNew(task_.session_suffix);
	session->AddSource(xop::channel_0, xop::H264Source::CreateNew(task_.fps));
	auto session_id = rtsp_server_->AddSession(session);

	ai::H264Encoder encoder(task_.width, task_.height, task_.fps);
	encoder.SetOutputCallback([&](const uint8_t* data, size_t len, bool key) {
		xop::AVFrame frame(len);
		frame.type      = key ? xop::VIDEO_FRAME_I : xop::VIDEO_FRAME_P;
		frame.timestamp = xop::H264Source::GetTimestamp();
		std::memcpy(frame.buffer.get(), data, len);
		rtsp_server_->PushFrame(session_id, xop::channel_0, frame);
	});

	if (!encoder.Open()) {
		rtsp_server_->RemoveSession(session_id);
		source->Close();
		return { PipelineExitCode::Failed, "open H264Encoder failed" };
	}

	std::cout << "[Pipeline] stream=" << task_.stream_id
	          << " node=" << node_id_
	          << " mode=legacy"
	          << " rtsp://localhost:" << task_.rtsp_port
	          << "/" << task_.session_suffix << std::endl;

	cv::Mat frame;
	int frame_count = 0;
	double latency_acc = 0.0;

	while (!stop_flag.load()) {
		STREAMSIGHT_LATENCY_SCOPE_WITH_IDS("control", "pipeline_run",
		                                   task_.stream_id, frame_count);
		auto t0 = steady_clock::now();

		if (!source->GrabFrame(frame)) {
			encoder.Close();
			rtsp_server_->RemoveSession(session_id);
			source->Close();
			return { PipelineExitCode::Completed, "source eof or error" };
		}

		if (frame.empty()) {
			continue;
		}

		if (frame.cols != task_.width || frame.rows != task_.height) {
			cv::resize(frame, frame, cv::Size(task_.width, task_.height));
		}

		if (ai_enabled && analyzer) {
			ai::AnalysisResult result = analyzer->Analyze(frame);
			if (overlay) overlay->Draw(frame, result);
		}

		encoder.EncodeFrame(frame);

		auto t1 = steady_clock::now();
		double latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
		latency_acc += latency_ms;
		++frame_count;

		if (metrics_) {
			metrics_->SetGauge("stream." + task_.stream_id, "pipeline_latency_ms", latency_ms);
			metrics_->IncCounter("stream." + task_.stream_id, "frame_count", 1.0);

			const double avg = latency_acc / std::max(1, frame_count);
			metrics_->SetGauge("node." + node_id_, "avg_pipeline_latency_ms", avg);
			metrics_->SetGauge("stream." + task_.stream_id, "avg_pipeline_latency_ms", avg);
		}
	}

	encoder.Close();
	rtsp_server_->RemoveSession(session_id);
	source->Close();
	return { PipelineExitCode::Stopped, "stop requested" };
}

} // namespace control
