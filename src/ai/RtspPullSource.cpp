// RtspPullSource.cpp

#include "RtspPullSource.h"
#include <iostream>

namespace ai {

RtspPullSource::RtspPullSource(const std::string& url, const std::string& transport)
    : url_(url), transport_(transport)
{}

bool RtspPullSource::Open() {
    // Set RTSP transport via environment variable before opening
    // (OpenCV's FFmpeg backend respects OPENCV_FFMPEG_CAPTURE_OPTIONS)
    std::string opts = "rtsp_transport;" + transport_ + ";stimeout;5000000";
    cap_.open(url_, cv::CAP_FFMPEG);
    if (!cap_.isOpened()) {
        std::cerr << "[RtspPullSource] Cannot open: " << url_ << std::endl;
        return false;
    }
    return true;
}

void RtspPullSource::Close() {
    cap_.release();
}

bool RtspPullSource::IsOpened() const {
    return cap_.isOpened();
}

bool RtspPullSource::GrabFrame(cv::Mat& frame) {
    if (!cap_.isOpened()) return false;
    bool ok = cap_.read(frame);
    if (!ok || frame.empty()) {
        std::cerr << "[RtspPullSource] Stream interrupted." << std::endl;
        return false;
    }
    return true;
}

double RtspPullSource::GetFPS() const {
    return cap_.isOpened() ? cap_.get(cv::CAP_PROP_FPS) : 0.0;
}

int RtspPullSource::GetWidth() const {
    return cap_.isOpened() ? (int)cap_.get(cv::CAP_PROP_FRAME_WIDTH) : 0;
}

int RtspPullSource::GetHeight() const {
    return cap_.isOpened() ? (int)cap_.get(cv::CAP_PROP_FRAME_HEIGHT) : 0;
}

} // namespace ai
