// FFmpegUtils.h
// Utility functions: error strings, AVFrame-to-cv::Mat bridge, timestamp helpers.

#ifndef FFMPEG_UTILS_H
#define FFMPEG_UTILS_H

#include <opencv2/opencv.hpp>
#include <string>
#include <cstdint>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}

namespace ffmpeg {

// Human-readable FFmpeg error string from return code.
std::string av_err_str(int errnum);

// Frame wrapper passed to AI interception callback.
struct FFmpegFrame {
    int      width;
    int      height;
    int      linesize;      // bytes per row (may include padding)
    uint8_t* data;          // BGR24 pixel data (owned by FFmpegStreamer)
    int64_t  pts;           // original decoder PTS in stream timebase
    int64_t  frame_index;   // sequential counter from stream start
    bool     is_keyframe;
};

// Zero-copy: wrap FFmpegFrame BGR24 data as cv::Mat for AI processing.
// The returned cv::Mat references frame.data directly — caller must not
// let the cv::Mat outlive the FFmpegFrame.
inline cv::Mat FrameToMat(const FFmpegFrame& f) {
    return cv::Mat(f.height, f.width, CV_8UC3, f.data, (size_t)f.linesize);
}

// Convert encoder PTS (in {1, fps} timebase) to 90kHz RTSP timestamp.
inline uint32_t PtsTo90kHz(int64_t pts, int fps) {
    return (uint32_t)(pts * 90000 / fps);
}

// Convert AVRational to double (e.g., {1, 25} → 0.04).
inline double AvRationalToDouble(AVRational r) {
    return r.den ? (double)r.num / (double)r.den : 0.0;
}

} // namespace ffmpeg

#endif // FFMPEG_UTILS_H
