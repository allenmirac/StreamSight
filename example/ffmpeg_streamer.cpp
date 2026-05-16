// ffmpeg_streamer.cpp
// Standalone demo: FFmpeg C API full pipeline with AI analysis and RTSP push.
//
// Build: make ffmpeg_streamer
// Run:   ./bin/ffmpeg_streamer --input test.h264 --port 8554
//        ./bin/ffmpeg_streamer --input test.h264 --rtmp rtmp://localhost/live/test --port 8554
//        ./bin/ffmpeg_streamer --input /dev/video0 --source camera --port 8554

#include "ffmpeg/FFmpegStreamer.h"
#include "ffmpeg/StreamPipeline.h"
#include "ffmpeg/PipelineManager.h"
#include "ffmpeg/RtspOutputAdapter.h"
#include "ffmpeg/RtmpOutputAdapter.h"
#include "ffmpeg/MultiOutputAdapter.h"
#include "ffmpeg/FFmpegUtils.h"

// Existing AI pipeline (same as rtsp_analysis_server)
#include "xop/RtspServer.h"
#include "xop/H264Source.h"
#include "xop/AACSource.h"
#include "net/EventLoop.h"
#include "ai/FaceDetector.h"
#include "ai/FaceRecognizer.h"
#include "ai/FaceDatabase.h"
#include "ai/FrameAnalyzer.h"
#include "ai/FrameOverlay.h"
#include "ai/HttpApiServer.h"
#include "ai/EventLogger.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <map>
#include <csignal>
#include <cstdlib>

extern "C" {
#include <libavutil/samplefmt.h>
}

static std::atomic<bool> g_stop{false};
static void OnSignal(int) { g_stop = true; }

static std::map<std::string, std::string> ParseArgs(int argc, char** argv) {
    std::map<std::string, std::string> args;
    args["source"]       = "file";
    args["input"]        = "test.h264";
    args["width"]        = "640";
    args["height"]       = "480";
    args["fps"]          = "25";
    args["port"]         = "8554";
    args["http-port"]    = "8080";
    args["bitrate"]      = "2000000";
    args["suffix"]       = "live";
    args["detect-model"] = "./models/face_detection.onnx";
    args["recog-model"]  = "./models/face_recognition.onnx";
    args["db"]           = "faces.json";
    args["log"]          = "events.jsonl";
    args["analyze-fps"]  = "5";
    args["no-ai"]        = "0";
    args["rtmp"]         = "";
    args["threads"]      = "2";
    args["pipeline-mode"]  = "serial";
    args["ringbuf-size"]   = "4";
    args["max-frame-age-ms"] = "500";
    args["eventloop-threads"] = "2";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-ai") { args["no-ai"] = "1"; continue; }
        if (a.substr(0, 2) == "--" && i + 1 < argc) {
            args[a.substr(2)] = argv[++i];
        }
    }
    return args;
}

int main(int argc, char** argv) {
    signal(SIGINT,  OnSignal);
    signal(SIGTERM, OnSignal);

    auto args = ParseArgs(argc, argv);

    int  width  = std::stoi(args["width"]);
    int  height = std::stoi(args["height"]);
    int  fps    = std::stoi(args["fps"]);
    int  port   = std::stoi(args["port"]);
    int  http_port = std::stoi(args["http-port"]);
    int  bitrate   = std::stoi(args["bitrate"]);
    int  enc_threads = std::stoi(args["threads"]);
    bool no_ai   = (args["no-ai"] == "1");
    int  afps    = std::stoi(args["analyze-fps"]);
    bool is_camera = (args["source"] == "camera");

    // Build input URL for FFmpeg
    std::string input_url;
    if (is_camera) {
        input_url = "/dev/video" + args["input"];
    } else {
        input_url = args["input"];
    }

    std::cout << "[Main] FFmpegStreamer demo" << std::endl;
    std::cout << "[Main] input=" << input_url << " " << width << "x"
              << height << " @" << fps << "fps" << std::endl;

    // ── RTSP server (matches rtsp_analysis_server) ────────────────────────
    auto event_loop  = std::make_shared<xop::EventLoop>();
    auto rtsp_server = xop::RtspServer::Create(event_loop.get());

    if (!rtsp_server->Start("0.0.0.0", port)) {
        std::cerr << "[Main] RTSP server bind failed on port " << port
                  << std::endl;
        return 1;
    }

    xop::MediaSession* session = xop::MediaSession::CreateNew(args["suffix"]);
    session->AddSource(xop::channel_0, xop::H264Source::CreateNew(fps));
    session->AddSource(xop::channel_1, xop::AACSource::CreateNew(44100, 2, true));
    session->AddNotifyConnectedCallback(
        [](xop::MediaSessionId, std::string ip, uint16_t p) {
            std::cout << "[RTSP] Client connected: " << ip << ":" << p
                      << std::endl;
        });
    session->AddNotifyDisconnectedCallback(
        [](xop::MediaSessionId, std::string ip, uint16_t p) {
            std::cout << "[RTSP] Client disconnected: " << ip << ":" << p
                      << std::endl;
        });
    xop::MediaSessionId session_id = rtsp_server->AddSession(session);

    std::cout << "[Main] RTSP: rtsp://localhost:" << port
              << "/" << args["suffix"] << std::endl;

    // ── AI modules (same as rtsp_analysis_server) ────────────────────────
    std::unique_ptr<ai::FaceDetector>   detector;
    std::unique_ptr<ai::FaceRecognizer> recognizer;
    std::unique_ptr<ai::FaceDatabase>   database;
    std::unique_ptr<ai::FrameAnalyzer>  analyzer;
    std::unique_ptr<ai::FrameOverlay>   overlay;
    std::unique_ptr<ai::EventLogger>    logger;
    std::unique_ptr<ai::HttpApiServer>  api_server;

    if (!no_ai) {
        detector.reset(new ai::FaceDetector(args["detect-model"]));
        if (!detector->Load()) {
            std::cerr << "[Main] Warning: detector model not loaded"
                      << std::endl;
            detector.reset();
        }

        recognizer.reset(new ai::FaceRecognizer(args["recog-model"]));
        if (!recognizer->Load()) {
            std::cerr << "[Main] Warning: recognizer model not loaded"
                      << std::endl;
            recognizer.reset();
        }

        database.reset(new ai::FaceDatabase(args["db"]));
        database->Load();

        analyzer.reset(new ai::FrameAnalyzer(
            detector.get(), recognizer.get(), database.get()));
        analyzer->SetAnalyzeRate(afps);

        overlay.reset(new ai::FrameOverlay());

        logger.reset(new ai::EventLogger(args["log"]));
        logger->Open();
        analyzer->SetEventCallback([&](const ai::AnalysisResult& r) {
            if (logger) logger->Log(r);
        });

        api_server.reset(new ai::HttpApiServer(
            http_port, database.get(), recognizer.get()));
        api_server->Start();
        std::cout << "[Main] HTTP API: http://localhost:" << http_port
                  << std::endl;
    }

    // ── FFmpegStreamer pipeline ──────────────────────────────────────────
    ffmpeg::StreamerConfig cfg;
    cfg.input_url  = input_url;
    cfg.fps        = fps;
    cfg.bitrate    = bitrate;
    cfg.threads    = enc_threads;
    cfg.output_width  = width;
    cfg.output_height = height;
    cfg.reconnect_on_eof = (args["source"] == "rtsp");

    // AI interception callback
    if (analyzer) {
        cfg.frame_cb = [&](ffmpeg::FFmpegFrame& f) -> bool {
            cv::Mat mat = ffmpeg::FrameToMat(f);

            if (mat.cols != width || mat.rows != height) {
                cv::resize(mat, mat, cv::Size(width, height));
            }

            ai::AnalysisResult result = analyzer->Analyze(mat);

            if (overlay) {
                overlay->Draw(mat, result);
            }

            if (api_server) {
                api_server->UpdateResult(result);
                api_server->AddEvent(result);
            }

            return true;
        };
    }

    // RTSP output adapter
    auto rtsp_out = std::make_shared<ffmpeg::RtspOutputAdapter>(
        rtsp_server.get(), session_id, xop::channel_0);
    cfg.outputs.push_back(rtsp_out);

    // Audio callback: push decoded PCM frames to RTSP audio channel
    cfg.audio_cb = [&](const AVFrame* aframe) {
        // Build xop::AVFrame from AVFrame data
        int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)aframe->format);
        int data_size = aframe->nb_samples * aframe->channels * bytes_per_sample;

        xop::AVFrame xop_frame(data_size);
        xop_frame.type = xop::AUDIO_FRAME;
        xop_frame.size = data_size;
        xop_frame.timestamp = xop::H264Source::GetTimestamp();
        memcpy(xop_frame.buffer.get(), aframe->data[0], data_size);

        rtsp_server->PushFrame(session_id, xop::channel_1, xop_frame);
    };

    // Optional RTMP output
    std::string rtmp_url = args["rtmp"];
    if (!rtmp_url.empty()) {
        auto rtmp_out = std::make_shared<ffmpeg::RtmpOutputAdapter>(rtmp_url);
        cfg.outputs.push_back(rtmp_out);
        std::cout << "[Main] RTMP: " << rtmp_url << std::endl;
    }

    // ── Run ──────────────────────────────────────────────────────────────
    bool parallel_mode = (args["pipeline-mode"] == "parallel");
    ffmpeg::PipelineManager pipeline_mgr;

    // RTSP event loop thread
    int ev_threads = std::stoi(args["eventloop-threads"]);
    auto event_loop_parallel = std::make_shared<xop::EventLoop>((uint32_t)ev_threads);

    std::thread rtsp_thread([&]() {
        while (!g_stop) {
            event_loop->Loop();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    if (parallel_mode) {
        // ── Parallel mode: StreamPipeline (3-stage) ──────────────────────
        ffmpeg::PipelineConfig pcfg;
        pcfg.input_url  = cfg.input_url;
        pcfg.fps        = cfg.fps;
        pcfg.bitrate    = cfg.bitrate;
        pcfg.threads    = cfg.threads;
        pcfg.output_width  = cfg.output_width;
        pcfg.output_height = cfg.output_height;
        pcfg.frame_cb  = cfg.frame_cb;
        pcfg.outputs   = cfg.outputs;
        pcfg.decode_ring_size  = std::stoi(args["ringbuf-size"]);
        pcfg.process_ring_size = std::stoi(args["ringbuf-size"]);
        pcfg.drop_policy.max_frame_age_us = std::stoi(args["max-frame-age-ms"]) * 1000LL;
        pcfg.enable_backpressure = true;

        pipeline_mgr.AddStream("main", pcfg);
        std::cout << "[Main] Running in PARALLEL mode (3-stage pipeline). Ctrl+C to stop." << std::endl;

        while (!g_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        pipeline_mgr.StopAll();
    } else {
        // ── Serial mode: FFmpegStreamer (original) ───────────────────────
        ffmpeg::FFmpegStreamer streamer(cfg);

        std::thread pipeline_thread([&]() {
            streamer.Run(g_stop);
        });

        std::cout << "[Main] Running in SERIAL mode. Ctrl+C to stop." << std::endl;

        pipeline_thread.join();
    }

    g_stop = true;
    rtsp_thread.join();

    // Cleanup
    if (api_server) api_server->Stop();
    if (logger)     logger->Close();
    if (database)   database->Save();
    rtsp_server->RemoveSession(session_id);

    std::cout << "[Main] Done." << std::endl;
    return 0;
}
