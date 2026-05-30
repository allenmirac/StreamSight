// FFmpegStreamer.cpp
// Full pipeline: avformat_open_input → decode → BGR convert → AI callback
// → YUV convert → encode → output adapters.

#include "FFmpegStreamer.h"
#include "FFmpegUtils.h"
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

FFmpegStreamer::FFmpegStreamer(const StreamerConfig& cfg)
    : cfg_(cfg)
{}

FFmpegStreamer::~FFmpegStreamer() {
    Close();
}

// ─── Open ────────────────────────────────────────────────────────────────────

bool FFmpegStreamer::Open() {
    if (opened_) return true;

    if (!OpenInput())  return false;
    if (!OpenDecoder()) return false;
    if (audio_idx_ >= 0 && cfg_.audio_cb) {
        if (!OpenAudioDecoder()) return false;
    }
    if (!OpenScalers()) return false;
    if (!OpenEncoder()) return false;
    if (!OpenOutputs()) return false;

    opened_ = true;
    std::cout << "[FFmpegStreamer] opened: " << cfg_.input_url
              << " " << dec_width_ << "x" << dec_height_
              << " @" << cfg_.fps << " fps  bitrate=" << cfg_.bitrate
              << std::endl;
    return true;
}

// ─── OpenInput ───────────────────────────────────────────────────────────────

bool FFmpegStreamer::OpenInput() {
    AVFormatContext* ctx = nullptr;

    AVDictionary* opts = nullptr;
    if (cfg_.open_timeout_ms > 0) {
        av_dict_set_int(&opts, "timeout", cfg_.open_timeout_ms * 1000, 0);
    }
    if (cfg_.read_timeout_ms > 0) {
        av_dict_set_int(&opts, "stimeout", cfg_.read_timeout_ms * 1000, 0);
    }
    // TCP transport preferred for RTSP reliability
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);

    int ret = avformat_open_input(&ctx, cfg_.input_url.c_str(),
                                  nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        std::cerr << "[FFmpegStreamer] avformat_open_input("
                  << cfg_.input_url << "): " << av_err_str(ret) << std::endl;
        return false;
    }
    ifmt_ctx_ = ctx;

    ret = avformat_find_stream_info(ifmt_ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "[FFmpegStreamer] avformat_find_stream_info: "
                  << av_err_str(ret) << std::endl;
        return false;
    }

    // Find first video and audio streams
    video_idx_ = -1;
    audio_idx_ = -1;
    for (unsigned i = 0; i < ifmt_ctx_->nb_streams; ++i) {
        auto type = ifmt_ctx_->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && video_idx_ < 0) {
            video_idx_ = (int)i;
        } else if (type == AVMEDIA_TYPE_AUDIO && audio_idx_ < 0) {
            audio_idx_ = (int)i;
        }
    }
    if (video_idx_ < 0) {
        std::cerr << "[FFmpegStreamer] no video stream found" << std::endl;
        return false;
    }
    if (audio_idx_ >= 0) {
        std::cout << "[FFmpegStreamer] audio stream found: idx="
                  << audio_idx_ << std::endl;
    }

    AVStream* vs = ifmt_ctx_->streams[video_idx_];
    src_tb_ = vs->time_base;
    dec_width_  = vs->codecpar->width;
    dec_height_ = vs->codecpar->height;
    dec_pix_fmt_ = (AVPixelFormat)vs->codecpar->format;

    std::cout << "[FFmpegStreamer] input: " << dec_width_ << "x"
              << dec_height_ << " pix_fmt=" << (int)dec_pix_fmt_
              << " tb=" << src_tb_.num << "/" << src_tb_.den << std::endl;

    return true;
}

// ─── OpenDecoder ─────────────────────────────────────────────────────────────

bool FFmpegStreamer::OpenDecoder() {
    AVStream* vs = ifmt_ctx_->streams[video_idx_];

    decoder_ = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!decoder_) {
        std::cerr << "[FFmpegStreamer] avcodec_find_decoder failed"
                  << std::endl;
        return false;
    }

    dec_ctx_ = avcodec_alloc_context3(decoder_);
    if (!dec_ctx_) return false;

    int ret = avcodec_parameters_to_context(dec_ctx_, vs->codecpar);
    if (ret < 0) {
        std::cerr << "[FFmpegStreamer] avcodec_parameters_to_context: "
                  << av_err_str(ret) << std::endl;
        return false;
    }

    // Single-threaded decode (each stream has its own pipeline thread)
    dec_ctx_->thread_count = 1;

    ret = avcodec_open2(dec_ctx_, decoder_, nullptr);
    if (ret < 0) {
        std::cerr << "[FFmpegStreamer] avcodec_open2(decoder): "
                  << av_err_str(ret) << std::endl;
        return false;
    }

    decoded_ = av_frame_alloc();
    if (!decoded_) return false;

    return true;
}

// ─── OpenAudioDecoder ────────────────────────────────────────────────────────

bool FFmpegStreamer::OpenAudioDecoder() {
    AVStream* as = ifmt_ctx_->streams[audio_idx_];
    const AVCodec* a_dec = avcodec_find_decoder(as->codecpar->codec_id);
    if (!a_dec) {
        std::cerr << "[FFmpegStreamer] audio decoder not found" << std::endl;
        return false;
    }

    audio_dec_ctx_ = avcodec_alloc_context3(a_dec);
    if (!audio_dec_ctx_) return false;

    int ret = avcodec_parameters_to_context(audio_dec_ctx_, as->codecpar);
    if (ret < 0) {
        std::cerr << "[FFmpegStreamer] audio avcodec_parameters_to_context: "
                  << av_err_str(ret) << std::endl;
        return false;
    }

    audio_dec_ctx_->thread_count = 1;
    ret = avcodec_open2(audio_dec_ctx_, a_dec, nullptr);
    if (ret < 0) {
        std::cerr << "[FFmpegStreamer] avcodec_open2(audio): "
                  << av_err_str(ret) << std::endl;
        return false;
    }

    std::cout << "[FFmpegStreamer] audio decoder: "
              << a_dec->name << " "
              << audio_dec_ctx_->sample_rate << "Hz "
              << audio_dec_ctx_->channels << "ch" << std::endl;
    return true;
}

// ─── ProcessAudioPacket ──────────────────────────────────────────────────────

void FFmpegStreamer::ProcessAudioPacket(AVPacket* pkt) {
    if (!audio_dec_ctx_ || !cfg_.audio_cb) return;

    int ret = avcodec_send_packet(audio_dec_ctx_, pkt);
    if (ret < 0) return;

    AVFrame* aframe = av_frame_alloc();
    while (avcodec_receive_frame(audio_dec_ctx_, aframe) == 0) {
        cfg_.audio_cb(aframe);
        av_frame_unref(aframe);
    }
    av_frame_free(&aframe);
}

// ─── OpenScalers ─────────────────────────────────────────────────────────────

bool FFmpegStreamer::OpenScalers() {
    // Decoder format → BGR24 (for AI interception)
    to_bgr_ = sws_getContext(
        dec_width_, dec_height_, dec_pix_fmt_,
        dec_width_, dec_height_, AV_PIX_FMT_BGR24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!to_bgr_) {
        std::cerr << "[FFmpegStreamer] sws_getContext(to_bgr) failed"
                  << std::endl;
        return false;
    }

    // BGR24 → YUV420P (for encoder input)
    int dst_w = cfg_.output_width  > 0 ? cfg_.output_width  : dec_width_;
    int dst_h = cfg_.output_height > 0 ? cfg_.output_height : dec_height_;
    to_enc_ = sws_getContext(
        dec_width_, dec_height_, AV_PIX_FMT_BGR24,
        dst_w, dst_h, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!to_enc_) {
        std::cerr << "[FFmpegStreamer] sws_getContext(to_enc) failed"
                  << std::endl;
        return false;
    }

    // Allocate persistent BGR24 frame (AI writes overlay into this buffer)
    bgr_ = av_frame_alloc();
    bgr_->format = AV_PIX_FMT_BGR24;
    bgr_->width  = dec_width_;
    bgr_->height = dec_height_;
    av_frame_get_buffer(bgr_, 0);

    // Allocate persistent encoder input frame (YUV420P)
    enc_in_ = av_frame_alloc();
    enc_in_->format = AV_PIX_FMT_YUV420P;
    enc_in_->width  = dst_w;
    enc_in_->height = dst_h;
    av_frame_get_buffer(enc_in_, 0);

    return true;
}

// ─── OpenEncoder ─────────────────────────────────────────────────────────────

bool FFmpegStreamer::OpenEncoder() {
    encoder_ = avcodec_find_encoder_by_name(cfg_.codec_name.c_str());
    if (!encoder_) {
        std::cerr << "[FFmpegStreamer] avcodec_find_encoder_by_name("
                  << cfg_.codec_name << ") failed" << std::endl;
        return false;
    }

    enc_ctx_ = avcodec_alloc_context3(encoder_);
    if (!enc_ctx_) return false;

    int dst_w = cfg_.output_width  > 0 ? cfg_.output_width  : dec_width_;
    int dst_h = cfg_.output_height > 0 ? cfg_.output_height : dec_height_;
    int gop = cfg_.gop_size > 0 ? cfg_.gop_size : cfg_.fps;

    enc_ctx_->width     = dst_w;
    enc_ctx_->height    = dst_h;
    enc_ctx_->pix_fmt   = AV_PIX_FMT_YUV420P;
    enc_ctx_->time_base = AVRational{1, cfg_.fps};
    enc_ctx_->framerate = AVRational{cfg_.fps, 1};
    enc_ctx_->bit_rate  = cfg_.bitrate;
    enc_ctx_->gop_size  = gop;
    enc_ctx_->max_b_frames = 0;          // no B-frames for low latency
    enc_ctx_->thread_count = cfg_.threads;

    // x264 preset / tune
    av_opt_set(enc_ctx_->priv_data, "preset", cfg_.preset.c_str(), 0);
    av_opt_set(enc_ctx_->priv_data, "tune",   cfg_.tune.c_str(),   0);
    // Slice-based threading reduces idle threads vs frame threading
    av_opt_set(enc_ctx_->priv_data, "x264-params", "sliced-threads=1", 0);

    // Global header for RTSP/RTP (SPS/PPS in extradata)
    if (enc_ctx_->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        // already set
    }

    int ret = avcodec_open2(enc_ctx_, encoder_, nullptr);
    if (ret < 0) {
        std::cerr << "[FFmpegStreamer] avcodec_open2(encoder): "
                  << av_err_str(ret) << std::endl;
        return false;
    }

    std::cout << "[FFmpegStreamer] encoder: " << cfg_.codec_name
              << " threads=" << cfg_.threads
              << " gop=" << gop << " bitrate=" << cfg_.bitrate << std::endl;

    return true;
}

// ─── OpenOutputs ─────────────────────────────────────────────────────────────

bool FFmpegStreamer::OpenOutputs() {
    if (cfg_.outputs.empty()) {
        std::cerr << "[FFmpegStreamer] warning: no output adapters configured"
                  << std::endl;
        return true;  // allowed, just no output
    }

    OutputConfig out_cfg;
    out_cfg.fps = cfg_.fps;

    for (auto& adapter : cfg_.outputs) {
        if (!adapter->Open(enc_ctx_, out_cfg)) {
            std::cerr << "[FFmpegStreamer] output adapter Open failed"
                      << std::endl;
            return false;
        }
    }
    return true;
}

// ─── Close ───────────────────────────────────────────────────────────────────

void FFmpegStreamer::Close() {
    if (!opened_) return;
    opened_ = false;

    // Flush encoder
    if (enc_ctx_) {
        avcodec_send_frame(enc_ctx_, nullptr);
        AVPacket* flush_pkt = av_packet_alloc();
        while (avcodec_receive_packet(enc_ctx_, flush_pkt) == 0) {
            for (auto& out : cfg_.outputs) {
                out->WritePacket(flush_pkt);
            }
            av_packet_unref(flush_pkt);
        }
        av_packet_free(&flush_pkt);
    }

    // Close output adapters. Keep the shared_ptrs in cfg_.outputs so
    // a subsequent Open() can re-open them (reconnect scenario).
    for (auto& out : cfg_.outputs) {
        out->Close();
    }

    // Free scalers
    if (to_bgr_) { sws_freeContext(to_bgr_); to_bgr_ = nullptr; }
    if (to_enc_) { sws_freeContext(to_enc_); to_enc_ = nullptr; }

    // Free frames
    if (enc_in_)  { av_frame_free(&enc_in_); }
    if (bgr_)     { av_frame_free(&bgr_); }
    if (decoded_) { av_frame_free(&decoded_); }

    // Free codec contexts
    if (enc_ctx_) { avcodec_free_context(&enc_ctx_); }
    if (dec_ctx_)       { avcodec_free_context(&dec_ctx_); }
    if (audio_dec_ctx_) { avcodec_free_context(&audio_dec_ctx_); }

    // Close input
    if (ifmt_ctx_) {
        avformat_close_input(&ifmt_ctx_);
        ifmt_ctx_ = nullptr;
    }

    dec_width_   = 0;
    dec_height_  = 0;
    frame_seq_   = 0;
    video_idx_   = -1;

    std::cout << "[FFmpegStreamer] closed" << std::endl;
}

// ─── ReadAndDecode ───────────────────────────────────────────────────────────

bool FFmpegStreamer::ReadAndDecode(AVFrame* decoded) {
    AVPacket* pkt = av_packet_alloc();
    int ret;

    // Read packets until we get a decoded frame or EOF/error
    while (true) {
        ret = av_read_frame(ifmt_ctx_, pkt);
        if (ret == AVERROR_EOF) {
            av_packet_free(&pkt);
            return false;  // end of stream
        }
        if (ret < 0) {
            std::cerr << "[FFmpegStreamer] av_read_frame: "
                      << av_err_str(ret) << std::endl;
            av_packet_free(&pkt);
            if (cfg_.reconnect_on_eof) {
                return false;  // caller will attempt reconnect
            }
            return false;
        }

        // Handle audio packets: decode and dispatch via audio_cb
        if (pkt->stream_index == audio_idx_ && cfg_.audio_cb) {
            ProcessAudioPacket(pkt);
            av_packet_unref(pkt);
            continue;
        }

        // Skip non-video, non-audio packets
        if (pkt->stream_index != video_idx_) {
            av_packet_unref(pkt);
            continue;
        }

        // Send to decoder
        ret = avcodec_send_packet(dec_ctx_, pkt);
        av_packet_unref(pkt);

        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            std::cerr << "[FFmpegStreamer] avcodec_send_packet: "
                      << av_err_str(ret) << std::endl;
            av_packet_free(&pkt);
            continue;  // skip bad packets, keep trying
        }

        // Try to receive a decoded frame
        ret = avcodec_receive_frame(dec_ctx_, decoded);
        if (ret == 0) {
            av_packet_free(&pkt);
            return true;  // got a frame
        }
        if (ret == AVERROR(EAGAIN)) {
            // Need more packets — loop back
            continue;
        }
        // Decode error
        std::cerr << "[FFmpegStreamer] avcodec_receive_frame: "
                  << av_err_str(ret) << std::endl;
        av_packet_free(&pkt);
        return false;
    }
}

// ─── EncodeAndDeliver ────────────────────────────────────────────────────────

bool FFmpegStreamer::EncodeAndDeliver(AVFrame* enc_in) {
    int ret = avcodec_send_frame(enc_ctx_, enc_in);
    if (ret < 0) {
        std::cerr << "[FFmpegStreamer] avcodec_send_frame: "
                  << av_err_str(ret) << std::endl;
        return false;
    }

    AVPacket* out_pkt = av_packet_alloc();
    while (true) {
        ret = avcodec_receive_packet(enc_ctx_, out_pkt);
        if (ret == AVERROR(EAGAIN)) {
            break;  // no more packets for this frame
        }
        if (ret < 0) {
            std::cerr << "[FFmpegStreamer] avcodec_receive_packet: "
                      << av_err_str(ret) << std::endl;
            av_packet_free(&out_pkt);
            return false;
        }

        // Deliver to all output adapters
        for (auto& out : cfg_.outputs) {
            if (out && out->IsOpened()) {
                out->WritePacket(out_pkt);
            }
        }
        av_packet_unref(out_pkt);
    }
    av_packet_free(&out_pkt);
    return true;
}

// ─── ProcessNextFrame ────────────────────────────────────────────────────────

bool FFmpegStreamer::ProcessNextFrame() {
    if (!opened_) return false;

    // 1. Demux + decode → decoded_ AVFrame
    auto capture_time = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    if (!ReadAndDecode(decoded_)) {
        return false;  // EOF or error
    }

    // 2. Colorspace: decoder fmt → BGR24 (into persistent bgr_ buffer)
    sws_scale(to_bgr_,
              (const uint8_t* const*)decoded_->data,
              decoded_->linesize,
              0, dec_height_,
              bgr_->data, bgr_->linesize);

    // 3. AI interception callback (in-place overlay)
    if (cfg_.frame_cb) {
        FFmpegFrame ff_frame;
        ff_frame.width       = dec_width_;
        ff_frame.height      = dec_height_;
        ff_frame.linesize    = bgr_->linesize[0];
        ff_frame.data        = bgr_->data[0];
        ff_frame.pts         = decoded_->pts;
        ff_frame.frame_index = frame_seq_;
        ff_frame.is_keyframe = (decoded_->key_frame != 0);
        ff_frame.capture_time_us = capture_time;

        if (!cfg_.frame_cb(ff_frame)) {
            // Callback returned false — drop this frame
            av_frame_unref(decoded_);
            return true;
        }
    }

    // 4. Colorspace: BGR24 → YUV420P (encoder input)
    sws_scale(to_enc_,
              (const uint8_t* const*)bgr_->data,
              bgr_->linesize,
              0, dec_height_,
              enc_in_->data, enc_in_->linesize);

    // 5. Set PTS
    enc_in_->pts = frame_seq_;

    // 6. Encode + deliver to outputs
    EncodeAndDeliver(enc_in_);

    // 7. Cleanup
    av_frame_unref(decoded_);
    frame_seq_++;

    return true;
}

// ─── Run ─────────────────────────────────────────────────────────────────────

void FFmpegStreamer::Run(std::atomic<bool>& stop_flag) {
    if (!Open()) {
        std::cerr << "[FFmpegStreamer] Open failed" << std::endl;
        return;
    }

    int reconnect_attempts = 0;

    while (!stop_flag) {
        if (!ProcessNextFrame()) {
            // EOF or error — attempt reconnect if configured
            if (!cfg_.reconnect_on_eof || !Reconnect()) {
                break;
            }
            reconnect_attempts = 0;
            continue;
        }
        reconnect_attempts = 0;
    }

    Close();
}

// ─── Reconnect ───────────────────────────────────────────────────────────────

bool FFmpegStreamer::Reconnect() {
    if (!cfg_.reconnect_on_eof) return false;

    // Close everything except config
    Close();
    opened_ = false;

    for (int i = 0; i < cfg_.max_reconnect; ++i) {
        std::cout << "[FFmpegStreamer] reconnect attempt " << (i + 1)
                  << "/" << cfg_.max_reconnect
                  << " in " << cfg_.reconnect_delay_ms << "ms..." << std::endl;

        std::this_thread::sleep_for(
            std::chrono::milliseconds(cfg_.reconnect_delay_ms));

        if (FFmpegStreamer::Open()) {
            frame_seq_ = 0;
            std::cout << "[FFmpegStreamer] reconnected" << std::endl;
            return true;
        }
    }

    std::cerr << "[FFmpegStreamer] reconnect failed after "
              << cfg_.max_reconnect << " attempts" << std::endl;
    return false;
}

} // namespace ffmpeg
