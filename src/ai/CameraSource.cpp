// CameraSource.cpp

#include "CameraSource.h"
#include <iostream>

namespace ai {

CameraSource::CameraSource(int device_index, int width, int height, double fps)
    : device_index_(device_index)
    , req_width_(width)
    , req_height_(height)
    , req_fps_(fps)
{}

bool CameraSource::Open() {
    cap_.open(device_index_, cv::CAP_V4L2);
    if (!cap_.isOpened()) {
        // Fallback: let OpenCV auto-select backend
        cap_.open(device_index_);
    }
    if (!cap_.isOpened()) {
        std::cerr << "[CameraSource] Cannot open device " << device_index_ << std::endl;
        return false;
    }
    if (req_width_  > 0) cap_.set(cv::CAP_PROP_FRAME_WIDTH,  req_width_);
    if (req_height_ > 0) cap_.set(cv::CAP_PROP_FRAME_HEIGHT, req_height_);
    if (req_fps_    > 0) cap_.set(cv::CAP_PROP_FPS,          req_fps_);
    return true;
}

void CameraSource::Close() {
    cap_.release();
}

bool CameraSource::IsOpened() const {
    return cap_.isOpened();
}

bool CameraSource::GrabFrame(cv::Mat& frame) {
    if (!cap_.isOpened()) return false;
    return cap_.read(frame);
}

double CameraSource::GetFPS() const {
    return cap_.isOpened() ? cap_.get(cv::CAP_PROP_FPS) : 0.0;
}

int CameraSource::GetWidth() const {
    return cap_.isOpened() ? (int)cap_.get(cv::CAP_PROP_FRAME_WIDTH) : 0;
}

int CameraSource::GetHeight() const {
    return cap_.isOpened() ? (int)cap_.get(cv::CAP_PROP_FRAME_HEIGHT) : 0;
}

} // namespace ai
