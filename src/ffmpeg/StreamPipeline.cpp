// StreamPipeline.cpp
// SRS-inspired 3-stage pipeline implementation.

#include "StreamPipeline.h"
#include "AudioOutputAdapter.h"
#include "FFmpegUtils.h"
#include "observe/LatencyTracer.h"
#include "observe/PithyPrint.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

namespace ffmpeg {

// ─── Constructor / Destructor ────────────────────────────────────────────────

StreamPipeline::StreamPipeline(const std::string& stream_id,
                               const PipelineConfig& cfg)
	: stream_id_(stream_id)
	, cfg_(cfg)
	, decode_ring_(cfg.decode_ring_size)
	, process_ring_(cfg.process_ring_size)
	, audio_ring_(cfg.audio_ring_size)
{}

StreamPipeline::~StreamPipeline() {
	Stop();
}

// ─── Start / Stop ───────────────────────────────────────────────────────────

bool StreamPipeline::Start() {
	if (running_) return true;

	stop_ = false;
	demux_thread_ = std::thread(&StreamPipeline::DemuxDecodeLoop, this);
	ai_thread_    = std::thread(&StreamPipeline::AIProcessLoop, this);
	encode_thread_ = std::thread(&StreamPipeline::EncodeOutputLoop, this);
	running_ = true;

	std::cout << "[StreamPipeline] " << stream_id_ << " started ("
	          << cfg_.decode_ring_size << "/" << cfg_.process_ring_size
	          << " ring buffers)" << std::endl;
	return true;
}

void StreamPipeline::Stop() {
	if (!running_) return;
	stop_ = true;
	running_ = false;

	if (demux_thread_.joinable())   demux_thread_.join();
	if (ai_thread_.joinable())      ai_thread_.join();
	if (encode_thread_.joinable())  encode_thread_.join();

	std::cout << "[StreamPipeline] " << stream_id_ << " stopped"
	          << "  decoded=" << frames_decoded_
	          << "  drop_demux=" << dropped_demux_
	          << "  drop_ai=" << dropped_ai_
	          << "  pruned_demux=" << pruned_demux_
	          << "  pruned_ai=" << pruned_ai_
	          << std::endl;
}

// ─── Ring fill queries ──────────────────────────────────────────────────────

int StreamPipeline::DecodeRingFill() const  { return decode_ring_.Size(); }
int StreamPipeline::ProcessRingFill() const { return process_ring_.Size(); }

// ─── DemuxDecodeLoop ────────────────────────────────────────────────────────

bool StreamPipeline::OpenDemuxer() {
	AVFormatContext* ctx = nullptr;

	AVDictionary* opts = nullptr;
	if (cfg_.open_timeout_ms > 0) {
		av_dict_set_int(&opts, "timeout", cfg_.open_timeout_ms * 1000, 0);
	}
	if (cfg_.read_timeout_ms > 0) {
		av_dict_set_int(&opts, "stimeout", cfg_.read_timeout_ms * 1000, 0);
	}
	av_dict_set(&opts, "rtsp_transport", "tcp", 0);

	int ret = avformat_open_input(&ctx, cfg_.input_url.c_str(), nullptr, &opts);
	av_dict_free(&opts);

	if (ret < 0) {
		std::cerr << "[StreamPipeline] avformat_open_input: "
		          << av_err_str(ret) << std::endl;
		return false;
	}
	ifmt_ctx_ = ctx;

	ret = avformat_find_stream_info(ifmt_ctx_, nullptr);
	if (ret < 0) {
		std::cerr << "[StreamPipeline] avformat_find_stream_info: "
		          << av_err_str(ret) << std::endl;
		return false;
	}

	video_idx_ = -1;
	audio_idx_ = -1;
	for (unsigned i = 0; i < ifmt_ctx_->nb_streams; ++i) {
		auto type = ifmt_ctx_->streams[i]->codecpar->codec_type;
		if (type == AVMEDIA_TYPE_VIDEO && video_idx_ < 0) {
			video_idx_ = (int)i;
		} else if (type == AVMEDIA_TYPE_AUDIO && audio_idx_ < 0 && cfg_.enable_audio) {
			audio_idx_ = (int)i;
		}
	}
	if (video_idx_ < 0) {
		std::cerr << "[StreamPipeline] no video stream" << std::endl;
		return false;
	}
	if (audio_idx_ >= 0) {
		std::cout << "[StreamPipeline] audio stream found: idx="
		          << audio_idx_ << std::endl;
	}

	AVStream* vs = ifmt_ctx_->streams[video_idx_];
	dec_width_  = vs->codecpar->width;
	dec_height_ = vs->codecpar->height;

	// Open decoder
	decoder_ = avcodec_find_decoder(vs->codecpar->codec_id);
	if (!decoder_) return false;

	dec_ctx_ = avcodec_alloc_context3(decoder_);
	if (!dec_ctx_) return false;

	ret = avcodec_parameters_to_context(dec_ctx_, vs->codecpar);
	if (ret < 0) return false;

	dec_ctx_->thread_count = 1;
	ret = avcodec_open2(dec_ctx_, decoder_, nullptr);
	if (ret < 0) return false;

	// Open audio decoder if present
	if (audio_idx_ >= 0) {
		AVStream* as = ifmt_ctx_->streams[audio_idx_];
		audio_decoder_ = avcodec_find_decoder(as->codecpar->codec_id);
		if (audio_decoder_) {
			audio_dec_ctx_ = avcodec_alloc_context3(audio_decoder_);
			int aret = avcodec_parameters_to_context(audio_dec_ctx_, as->codecpar);
			if (aret >= 0) {
				audio_dec_ctx_->thread_count = 1;
				aret = avcodec_open2(audio_dec_ctx_, audio_decoder_, nullptr);
				if (aret >= 0) {
					std::cout << "[StreamPipeline] audio decoder: "
					          << audio_decoder_->name << " "
					          << audio_dec_ctx_->sample_rate << "Hz "
					          << audio_dec_ctx_->channels << "ch" << std::endl;
				}
			}
		}
	}

	// Create BGR24 scaler
	to_bgr_ = sws_getContext(
		dec_width_, dec_height_, (AVPixelFormat)vs->codecpar->format,
		dec_width_, dec_height_, AV_PIX_FMT_BGR24,
		SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!to_bgr_) return false;

	std::cout << "[StreamPipeline] demuxer opened: " << dec_width_ << "x"
	          << dec_height_ << std::endl;
	return true;
}

void StreamPipeline::CloseDemuxer() {
	if (to_bgr_) { sws_freeContext(to_bgr_); to_bgr_ = nullptr; }
	if (dec_ctx_) { avcodec_free_context(&dec_ctx_); }
	if (audio_dec_ctx_) { avcodec_free_context(&audio_dec_ctx_); }
	if (ifmt_ctx_) { avformat_close_input(&ifmt_ctx_); ifmt_ctx_ = nullptr; }
	decoder_ = nullptr;
	audio_decoder_ = nullptr;
	video_idx_ = -1;
	audio_idx_ = -1;
}

bool StreamPipeline::ReadAndDecodeOnce(AVFrame* decoded) {
	AVPacket* pkt = av_packet_alloc();
	int ret;

	while (!stop_) {
		ret = av_read_frame(ifmt_ctx_, pkt);
		if (ret == AVERROR_EOF) {
			av_packet_free(&pkt);
			return false;
		}
		if (ret < 0) {
			av_packet_free(&pkt);
			return false;
		}

		// Handle audio packets: decode and push to audio ring buffer
		if (pkt->stream_index == audio_idx_) {
			if (audio_dec_ctx_) {
				int aret = avcodec_send_packet(audio_dec_ctx_, pkt);
				if (aret >= 0) {
					AVFrame* aframe = av_frame_alloc();
					while (avcodec_receive_frame(audio_dec_ctx_, aframe) == 0) {
						int bps = av_get_bytes_per_sample(
							(AVSampleFormat)aframe->format);
						int data_size = aframe->nb_samples *
							aframe->channels * bps;
						auto pcm = std::make_shared<std::vector<uint8_t>>(data_size);
						std::memcpy(pcm->data(), aframe->data[0], data_size);

						AudioFrame auframe;
						auframe.pcm_data = pcm;
						auframe.capture_time_us = std::chrono::duration_cast<
							std::chrono::microseconds>(
							std::chrono::steady_clock::now().time_since_epoch()
						).count();
						auframe.sample_rate = aframe->sample_rate;
						auframe.channels    = aframe->channels;
						auframe.nb_samples  = aframe->nb_samples;

						if (cfg_.enable_backpressure) {
							auto ts_getter = [](const AudioFrame& f) {
								return f.capture_time_us;
							};
							auto now_us = auframe.capture_time_us;
							auto status = audio_ring_.PushOrDrop(
								std::move(auframe), now_us,
								cfg_.drop_policy.max_frame_age_us, ts_getter);
							if (status == xop::RingBuffer<AudioFrame>::DropStatus::OldDropped) {
								++audio_dropped_;
							}
						} else {
							if (!audio_ring_.Push(std::move(auframe))) {
								audio_ring_.PushOverwrite(std::move(auframe));
								++audio_dropped_;
							}
						}
						av_frame_unref(aframe);
					}
					av_frame_free(&aframe);
				}
			}
			av_packet_unref(pkt);
			continue;
		}

		// Skip non-video, non-audio packets
		if (pkt->stream_index != video_idx_) {
			av_packet_unref(pkt);
			continue;
		}

		ret = avcodec_send_packet(dec_ctx_, pkt);
		av_packet_unref(pkt);

		if (ret < 0 && ret != AVERROR(EAGAIN)) {
			continue;
		}

		ret = avcodec_receive_frame(dec_ctx_, decoded);
		if (ret == 0) {
			av_packet_free(&pkt);
			return true;
		}
		if (ret == AVERROR(EAGAIN)) {
			continue;
		}
		av_packet_free(&pkt);
		return false;
	}
	av_packet_free(&pkt);
	return false;
}

void StreamPipeline::DemuxDecodeLoop() {
	if (!OpenDemuxer()) {
		std::cerr << "[StreamPipeline] " << stream_id_
		          << " demuxer open failed" << std::endl;
		stop_ = true;
		return;
	}

	// Pre-allocated frames
	AVFrame* decoded = av_frame_alloc();
	AVFrame* bgr     = av_frame_alloc();
	bgr->format = AV_PIX_FMT_BGR24;
	bgr->width  = dec_width_;
	bgr->height = dec_height_;
	av_frame_get_buffer(bgr, 0);

	int64_t frame_idx = 0;
	observe::PithyPrint pithy(5000);  // log at most every 5s

	while (!stop_) {
		// ── Client-aware gating: wait until at least one RTSP client is connected ──
		if (cfg_.has_clients && cfg_.client_cv && cfg_.client_mutex) {
			std::unique_lock<std::mutex> lock(*cfg_.client_mutex);
			cfg_.client_cv->wait(lock, [this] {
				return cfg_.has_clients->load() > 0 || stop_.load();
			});
		}
		if (stop_) break;

		auto capture_us = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();

		// Per-frame latency scope: measures demux+decode+scale+push
		STREAMSIGHT_LATENCY_SCOPE_WITH_IDS("pipeline", "demux_decode",
		                                    stream_id_, frame_idx);

		if (!ReadAndDecodeOnce(decoded)) {
			break;  // EOF or error
		}

		// Colorspace: decoder fmt → BGR24
		sws_scale(to_bgr_,
		          (const uint8_t* const*)decoded->data,
		          decoded->linesize,
		          0, dec_height_,
		          bgr->data, bgr->linesize);

		// Copy BGR24 data for cross-thread transfer
		int data_size = bgr->linesize[0] * dec_height_;
		auto pixel_data = std::make_shared<std::vector<uint8_t>>(data_size);
		std::memcpy(pixel_data->data(), bgr->data[0], data_size);

		DecodedFrame dframe;
		dframe.pixel_data      = pixel_data;
		dframe.width           = dec_width_;
		dframe.height          = dec_height_;
		dframe.linesize        = bgr->linesize[0];
		dframe.frame_index     = frame_idx;
		dframe.capture_time_us = capture_us;
		dframe.is_keyframe     = (decoded->key_frame != 0);

		av_frame_unref(decoded);

		// Push with backpressure
		if (cfg_.enable_backpressure) {
			auto ts_getter = [](const DecodedFrame& f) { return f.capture_time_us; };
			auto status = decode_ring_.PushOrDrop(
				std::move(dframe), capture_us,
				cfg_.drop_policy.max_frame_age_us, ts_getter);
			if (status == xop::RingBuffer<DecodedFrame>::DropStatus::OldDropped) {
				++dropped_demux_;
			}
		} else {
			if (!decode_ring_.Push(std::move(dframe))) {
				decode_ring_.PushOverwrite(std::move(dframe));
				++dropped_demux_;
			}
		}

		++frames_decoded_;
		++frame_idx;

		// Track peak fill
		int cur_fill = decode_ring_.Size();
		int prev_max = max_decode_ring_fill_.load();
		while (cur_fill > prev_max &&
		       !max_decode_ring_fill_.compare_exchange_weak(prev_max, cur_fill)) {}

		decode_cv_.notify_one();

		if (pithy.ShouldLog()) {
			std::cout << "[StreamPipeline " << stream_id_ << "] demux: "
			          << decode_ring_.Size() << "/" << decode_ring_.Capacity()
			          << " decode_ring  dropped=" << dropped_demux_
			          << " pruned=" << pruned_demux_
			          << "  frames=" << frames_decoded_ << std::endl;
		}
	}

	av_frame_free(&bgr);
	av_frame_free(&decoded);
	CloseDemuxer();
	stop_ = true;  // signal downstream stages
}

// ─── AIProcessLoop ──────────────────────────────────────────────────────────

void StreamPipeline::AIProcessLoop() {
	observe::PithyPrint pithy(5000);

	while (!stop_ || !decode_ring_.IsEmpty()) {
		// ── Sliding time window: proactively prune stale frames ──
		if (cfg_.enable_backpressure) {
			float fill_ratio = (float)decode_ring_.Size() / decode_ring_.Capacity();
			if (fill_ratio >= cfg_.drop_policy.start_drop_ratio) {
				backpressure_events_++;
				auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count();
				int64_t cutoff_us = now_us - cfg_.drop_policy.max_frame_age_us;
				auto ts_getter = [](const DecodedFrame& f) { return f.capture_time_us; };
				std::function<bool(const DecodedFrame&)> kf_fn = nullptr;
				if (cfg_.drop_policy.prefer_keep_keyframe) {
					kf_fn = [](const DecodedFrame& f) { return f.is_keyframe; };
				}
				int pruned = decode_ring_.PruneStale(cutoff_us, ts_getter, kf_fn);
				if (pruned > 0) {
					pruned_demux_ += pruned;
				}
			}
		}

		DecodedFrame dframe;
		if (!decode_ring_.Pop(dframe)) {
			if (stop_) break;
			std::unique_lock<std::mutex> lock(decode_cv_mutex_);
			decode_cv_.wait_for(lock, std::chrono::milliseconds(100),
				[this] { return stop_.load() || !decode_ring_.IsEmpty(); });
			continue;
		}

		// Rate control: skip AI if decode_ring_ is backing up
		bool skip_ai = false;
		if (cfg_.enable_backpressure) {
			float fill_ratio = (float)decode_ring_.Size() / decode_ring_.Capacity();
			float r = cfg_.drop_policy.start_drop_ratio;
			if (fill_ratio > r + 0.15f) {
				skip_ai = true;  // too far behind, skip AI entirely
				backpressure_events_++;
			} else if (fill_ratio > r && !dframe.is_keyframe) {
				backpressure_events_++;
				skip_ai = true;  // skip AI on non-keyframes when backlogged
			}
		}

		// Run AI callback
		if (cfg_.frame_cb && !skip_ai) {
			FFmpegFrame ff_frame;
			ff_frame.width       = dframe.width;
			ff_frame.height      = dframe.height;
			ff_frame.linesize    = dframe.linesize;
			ff_frame.data        = dframe.pixel_data->data();
			ff_frame.pts         = dframe.frame_index;
			ff_frame.frame_index = dframe.frame_index;
			ff_frame.is_keyframe = dframe.is_keyframe;
			ff_frame.capture_time_us = dframe.capture_time_us;

			cfg_.frame_cb(ff_frame);
			// Note: overlay is drawn in-place into dframe.pixel_data
		}

		ProcessedFrame pframe;
		pframe.pixel_data      = dframe.pixel_data;  // zero-copy share
		pframe.width           = dframe.width;
		pframe.height          = dframe.height;
		pframe.linesize        = dframe.linesize;
		pframe.frame_index     = dframe.frame_index;
		pframe.capture_time_us = dframe.capture_time_us;
		pframe.is_keyframe     = dframe.is_keyframe;

		// Push to encode ring
		if (cfg_.enable_backpressure) {
			auto ts_getter = [](const ProcessedFrame& f) { return f.capture_time_us; };
			auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			auto status = process_ring_.PushOrDrop(
				std::move(pframe), now_us,
				cfg_.drop_policy.max_frame_age_us, ts_getter);
			if (status == xop::RingBuffer<ProcessedFrame>::DropStatus::OldDropped) {
				++dropped_ai_;
			}
		} else {
			if (!process_ring_.Push(std::move(pframe))) {
				process_ring_.PushOverwrite(std::move(pframe));
				++dropped_ai_;
			}
		}
		process_cv_.notify_one();

		// Track peak fill (producer side, after push -- symmetric with decode_ring_)
		int proc_cur_fill = process_ring_.Size();
		int proc_prev_max = max_process_ring_fill_.load();
		while (proc_cur_fill > proc_prev_max &&
		       !max_process_ring_fill_.compare_exchange_weak(proc_prev_max, proc_cur_fill)) {}

		if (pithy.ShouldLog()) {
			std::cout << "[StreamPipeline " << stream_id_ << "] ai:  "
			          << decode_ring_.Size() << "/" << decode_ring_.Capacity()
			          << " -> " << process_ring_.Size() << "/" << process_ring_.Capacity()
			          << " process_ring"
			          << (skip_ai ? " (skipping AI)" : "")
			          << "  pruned_demux=" << pruned_demux_
			          << "  pruned_ai=" << pruned_ai_
			          << std::endl;
		}
	}

	stop_ = true;
}

// ─── EncodeOutputLoop ───────────────────────────────────────────────────────

void StreamPipeline::EncodeOutputLoop() {
	// Open encoder
	encoder_ = avcodec_find_encoder_by_name(cfg_.codec_name.c_str());
	if (!encoder_) {
		std::cerr << "[StreamPipeline] encoder not found: "
		          << cfg_.codec_name << std::endl;
		return;
	}

	enc_ctx_ = avcodec_alloc_context3(encoder_);
	if (!enc_ctx_) return;

	int dst_w = cfg_.output_width  > 0 ? cfg_.output_width  : 640;
	int dst_h = cfg_.output_height > 0 ? cfg_.output_height : 480;
	int gop = cfg_.gop_size > 0 ? cfg_.gop_size : cfg_.fps;

	enc_ctx_->width     = dst_w;
	enc_ctx_->height    = dst_h;
	enc_ctx_->pix_fmt   = AV_PIX_FMT_YUV420P;
	enc_ctx_->time_base = AVRational{1, cfg_.fps};
	enc_ctx_->framerate = AVRational{cfg_.fps, 1};
	enc_ctx_->bit_rate  = cfg_.bitrate;
	enc_ctx_->gop_size  = gop;
	enc_ctx_->max_b_frames = 0;
	enc_ctx_->thread_count = cfg_.threads;

	av_opt_set(enc_ctx_->priv_data, "preset", cfg_.preset.c_str(), 0);
	av_opt_set(enc_ctx_->priv_data, "tune",   cfg_.tune.c_str(),   0);
	av_opt_set(enc_ctx_->priv_data, "x264-params", "sliced-threads=1", 0);

	int ret = avcodec_open2(enc_ctx_, encoder_, nullptr);
	if (ret < 0) {
		std::cerr << "[StreamPipeline] avcodec_open2(encoder): "
		          << av_err_str(ret) << std::endl;
		return;
	}

	// Open output adapters
	OutputConfig out_cfg;
	out_cfg.fps = cfg_.fps;
	for (auto& adapter : cfg_.outputs) {
		if (!adapter->Open(enc_ctx_, out_cfg)) {
			std::cerr << "[StreamPipeline] output adapter Open failed" << std::endl;
		}
	}

	// Open audio output adapter
	AudioOutputAdapter audio_out(
		cfg_.audio_rtsp_server, cfg_.audio_session_id,
		cfg_.audio_channel, cfg_.audio_sample_rate, cfg_.audio_channels);
	bool audio_enabled = (cfg_.enable_audio &&
	                      cfg_.audio_rtsp_server != nullptr &&
	                      cfg_.audio_session_id > 0);
	if (audio_enabled) {
		audio_enabled = audio_out.Open();
	}

	// BGR24 → YUV420P scaler (use first frame dimensions or defaults)
	to_enc_ = sws_getContext(dst_w, dst_h, AV_PIX_FMT_BGR24,
	                         dst_w, dst_h, AV_PIX_FMT_YUV420P,
	                         SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!to_enc_) return;

	// Pre-allocated YUV420P frame
	enc_in_ = av_frame_alloc();
	enc_in_->format = AV_PIX_FMT_YUV420P;
	enc_in_->width  = dst_w;
	enc_in_->height = dst_h;
	av_frame_get_buffer(enc_in_, 0);

	observe::PithyPrint pithy(5000);
	int64_t frame_idx = 0;

	while (!stop_ || !process_ring_.IsEmpty() || !audio_ring_.IsEmpty()) {
		// ── Audio output: process pending audio frames ──
		if (audio_enabled) {
			while (!audio_ring_.IsEmpty()) {
				// Prune stale audio frames first
				if (cfg_.enable_backpressure) {
					auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
						std::chrono::steady_clock::now().time_since_epoch()).count();
					int64_t cutoff_us = now_us - cfg_.drop_policy.max_frame_age_us;
					auto ts_getter = [](const AudioFrame& f) { return f.capture_time_us; };
					int pruned = audio_ring_.PruneStale(cutoff_us, ts_getter, nullptr);
					if (pruned > 0) audio_pruned_ += pruned;
				}

				AudioFrame auframe;
				if (!audio_ring_.Pop(auframe)) break;

				// Wrap PCM in AVFrame for the AAC encoder
				AVFrame* pcm_frame = av_frame_alloc();
				pcm_frame->format      = AV_SAMPLE_FMT_S16;
				pcm_frame->sample_rate = auframe.sample_rate;
				pcm_frame->channel_layout = auframe.channels == 2 ?
					AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
				pcm_frame->channels    = auframe.channels;
				pcm_frame->nb_samples  = auframe.nb_samples;
				av_frame_get_buffer(pcm_frame, 0);
				std::memcpy(pcm_frame->data[0], auframe.pcm_data->data(),
				            auframe.pcm_data->size());

				audio_out.PushFrame(pcm_frame);
				av_frame_free(&pcm_frame);
			}
		}

		// ── Sliding time window: proactively prune stale video frames ──
		if (cfg_.enable_backpressure) {
			float fill_ratio = (float)process_ring_.Size() / process_ring_.Capacity();
			if (fill_ratio >= cfg_.drop_policy.start_drop_ratio) {
				backpressure_events_++;
				auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count();
				int64_t cutoff_us = now_us - cfg_.drop_policy.max_frame_age_us;
				auto ts_getter = [](const ProcessedFrame& f) { return f.capture_time_us; };
				std::function<bool(const ProcessedFrame&)> kf_fn = nullptr;
				if (cfg_.drop_policy.prefer_keep_keyframe) {
					kf_fn = [](const ProcessedFrame& f) { return f.is_keyframe; };
				}
				int pruned = process_ring_.PruneStale(cutoff_us, ts_getter, kf_fn);
				if (pruned > 0) {
					pruned_ai_ += pruned;
				}
			}
		}

		ProcessedFrame pframe;
		if (!process_ring_.Pop(pframe)) {
			if (stop_) break;
			std::unique_lock<std::mutex> lock(process_cv_mutex_);
			process_cv_.wait_for(lock, std::chrono::milliseconds(100),
				[this] { return stop_.load() || !process_ring_.IsEmpty(); });
			continue;
		}

		// Wrap BGR24 pixel data as AVFrame for scaling
		AVFrame* bgr_wrap = av_frame_alloc();
		bgr_wrap->format = AV_PIX_FMT_BGR24;
		bgr_wrap->width  = pframe.width;
		bgr_wrap->height = pframe.height;
		bgr_wrap->data[0] = pframe.pixel_data->data();
		bgr_wrap->linesize[0] = pframe.linesize;

		// Colorspace: BGR24 → YUV420P
		sws_scale(to_enc_,
		          (const uint8_t* const*)bgr_wrap->data,
		          bgr_wrap->linesize,
		          0, pframe.height,
		          enc_in_->data, enc_in_->linesize);
		av_frame_free(&bgr_wrap);

		// PTS from capture time: preserves real frame timing through pipeline
		if (frame_idx == 0) {
			pts_base_us_ = pframe.capture_time_us;
		}
		enc_in_->pts = (pframe.capture_time_us - pts_base_us_) * cfg_.fps / 1000000;
		++frame_idx;

		// Encode
		ret = avcodec_send_frame(enc_ctx_, enc_in_);
		if (ret < 0) continue;

		AVPacket* out_pkt = av_packet_alloc();
		while (avcodec_receive_packet(enc_ctx_, out_pkt) == 0) {
			for (auto& out : cfg_.outputs) {
				if (out && out->IsOpened()) {
					out->WritePacket(out_pkt);
				}
			}
			av_packet_unref(out_pkt);
		}
		av_packet_free(&out_pkt);

		// Log full pipeline latency (cross-thread: capture → encode output)
		{
			auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			int64_t pipeline_latency_us = now_us - pframe.capture_time_us;

			observe::LatencyEvent ev;
			ev.module      = "pipeline";
			ev.stage       = "full";
			ev.event       = "frame";
			ev.stream_id   = stream_id_;
			ev.frame_id    = pframe.frame_index;
			ev.duration_us = pipeline_latency_us;
			ev.timestamp_ms = now_us / 1000;
			observe::LatencyTracer::Instance().LogEvent(ev);
		}

		if (pithy.ShouldLog()) {
			std::cout << "[StreamPipeline " << stream_id_ << "] enc:  "
			          << process_ring_.Size() << "/" << process_ring_.Capacity()
			          << " process_ring  encoded=" << frame_idx << std::endl;
		}
	}

	// Flush encoder
	avcodec_send_frame(enc_ctx_, nullptr);
	AVPacket* flush_pkt = av_packet_alloc();
	while (avcodec_receive_packet(enc_ctx_, flush_pkt) == 0) {
		for (auto& out : cfg_.outputs) {
			out->WritePacket(flush_pkt);
		}
		av_packet_unref(flush_pkt);
	}
	av_packet_free(&flush_pkt);

	// Cleanup
	for (auto& out : cfg_.outputs) out->Close();
	if (audio_enabled) audio_out.Close();
	if (enc_in_)  av_frame_free(&enc_in_);
	if (to_enc_)  sws_freeContext(to_enc_);
	if (enc_ctx_) avcodec_free_context(&enc_ctx_);
}

} // namespace ffmpeg
