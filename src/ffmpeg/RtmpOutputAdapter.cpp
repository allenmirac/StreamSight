// RtmpOutputAdapter.cpp
// Muxes encoded H.264 packets to RTMP output via FFmpeg's FLV muxer.

#include "RtmpOutputAdapter.h"
#include "FFmpegUtils.h"
#include <cstdio>
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace ffmpeg {

RtmpOutputAdapter::RtmpOutputAdapter(const std::string& rtmp_url)
    : rtmp_url_(rtmp_url)
{}

RtmpOutputAdapter::~RtmpOutputAdapter() {
    Close();
}

bool RtmpOutputAdapter::Open(const AVCodecContext* enc_ctx,
                              const OutputConfig& /*cfg*/) {
    if (rtmp_url_.empty()) {
        std::cerr << "[RtmpOutput] empty RTMP URL" << std::endl;
        return false;
    }

    // Allocate output format context (FLV container for RTMP)
    int ret = avformat_alloc_output_context2(
        &ofmt_ctx_, nullptr, "flv", rtmp_url_.c_str());
    if (ret < 0 || !ofmt_ctx_) {
        std::cerr << "[RtmpOutput] avformat_alloc_output_context2: "
                  << av_err_str(ret) << std::endl;
        return false;
    }

    // Create video stream from encoder parameters
    video_stream_ = avformat_new_stream(ofmt_ctx_, nullptr);
    if (!video_stream_) {
        std::cerr << "[RtmpOutput] avformat_new_stream failed" << std::endl;
        return false;
    }

    ret = avcodec_parameters_from_context(video_stream_->codecpar, enc_ctx);
    if (ret < 0) {
        std::cerr << "[RtmpOutput] avcodec_parameters_from_context: "
                  << av_err_str(ret) << std::endl;
        return false;
    }
    video_stream_->time_base = enc_ctx->time_base;

    // Open I/O (RTMP connection)
    if (!(ofmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&ofmt_ctx_->pb, rtmp_url_.c_str(),
                         AVIO_FLAG_WRITE, nullptr, nullptr);
        if (ret < 0) {
            std::cerr << "[RtmpOutput] avio_open2: " << av_err_str(ret)
                      << " (" << rtmp_url_ << ")" << std::endl;
            return false;
        }
    }

    // Write header (RTMP handshake + metadata)
    ret = avformat_write_header(ofmt_ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "[RtmpOutput] avformat_write_header: "
                  << av_err_str(ret) << std::endl;
        return false;
    }

    opened_ = true;
    std::cout << "[RtmpOutput] connected to " << rtmp_url_ << std::endl;
    return true;
}

void RtmpOutputAdapter::Close() {
    if (!opened_) return;
    opened_ = false;

    if (ofmt_ctx_) {
        av_write_trailer(ofmt_ctx_);
        if (!(ofmt_ctx_->oformat->flags & AVFMT_NOFILE) && ofmt_ctx_->pb) {
            avio_closep(&ofmt_ctx_->pb);
        }
        avformat_free_context(ofmt_ctx_);
        ofmt_ctx_ = nullptr;
    }
    video_stream_ = nullptr;
    std::cout << "[RtmpOutput] disconnected" << std::endl;
}

bool RtmpOutputAdapter::WritePacket(const AVPacket* pkt) {
    if (!opened_ || !ofmt_ctx_ || !video_stream_) return false;

    AVPacket tmp = *pkt;
    tmp.stream_index = video_stream_->index;

    // Rescale PTS/DTS from encoder timebase to output stream timebase
    av_packet_rescale_ts(&tmp, video_stream_->time_base,
                         video_stream_->time_base);

    int ret = av_interleaved_write_frame(ofmt_ctx_, &tmp);
    if (ret < 0) {
        std::cerr << "[RtmpOutput] av_interleaved_write_frame: "
                  << av_err_str(ret) << std::endl;
        return false;
    }

    pts_counter_++;
    return true;
}

bool RtmpOutputAdapter::IsOpened() const {
    return opened_;
}

} // namespace ffmpeg
