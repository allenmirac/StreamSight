// FileSource.cpp

#include "FileSource.h"
#include <iostream>

namespace ai {

FileSource::FileSource(const std::string& path, bool loop)
    : path_(path), loop_(loop)
{}

bool FileSource::Open() {
    cap_.open(path_);
    if (!cap_.isOpened()) {
        std::cerr << "[FileSource] Cannot open file: " << path_ << std::endl;
        return false;
    }
    return true;
}

void FileSource::Close() {
    cap_.release();
}

bool FileSource::IsOpened() const {
    return cap_.isOpened();
}

bool FileSource::GrabFrame(cv::Mat& frame) {
    if (!cap_.isOpened()) return false;

    if (!cap_.read(frame) || frame.empty()) {
        if (!loop_) return false;
        // Rewind and retry once
        cap_.set(cv::CAP_PROP_POS_FRAMES, 0);
        if (!cap_.read(frame) || frame.empty()) return false;
    }
    return true;
}

double FileSource::GetFPS() const {
    return cap_.isOpened() ? cap_.get(cv::CAP_PROP_FPS) : 0.0;
}

int FileSource::GetWidth() const {
    return cap_.isOpened() ? (int)cap_.get(cv::CAP_PROP_FRAME_WIDTH) : 0;
}

int FileSource::GetHeight() const {
    return cap_.isOpened() ? (int)cap_.get(cv::CAP_PROP_FRAME_HEIGHT) : 0;
}

} // namespace ai
