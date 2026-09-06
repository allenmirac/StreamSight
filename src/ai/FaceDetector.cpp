// FaceDetector.cpp

#include "FaceDetector.h"
#include "observe/LatencyTracer.h"
#include <iostream>
#include <algorithm>

namespace ai {

FaceDetector::FaceDetector(const std::string& model_path,
                           float score_thresh, float nms_thresh,
                           cv::Size input_size)
    : model_path_(model_path)
    , score_thresh_(score_thresh)
    , nms_thresh_(nms_thresh)
    , input_size_(input_size)
{}

bool FaceDetector::Load() {
    try {
        // YuNet (YuFaceDetectNet) emits 12 multi-scale tensors; FaceDetectorYN
        // handles the anchor decode + NMS, so we don't hand-roll postprocessing.
        net_ = cv::FaceDetectorYN::create(model_path_, "",
                                          input_size_, score_thresh_, nms_thresh_,
                                          /*top_k=*/5000);
    } catch (const cv::Exception& e) {
        std::cerr << "[FaceDetector] Failed to load " << model_path_
                  << ": " << e.what() << std::endl;
        return false;
    }
    if (!net_) {
        std::cerr << "[FaceDetector] FaceDetectorYN is empty after loading." << std::endl;
        return false;
    }
    loaded_ = true;
    return true;
}

std::vector<FaceBox> FaceDetector::Detect(const cv::Mat& frame) {
    STREAMSIGHT_LATENCY_SCOPE("ai", "face_detection");
    if (!loaded_ || frame.empty()) return {};

    // YuNet's FPN skip-connections require a multiple-of-32 input, so run at
    // the fixed input_size_ and scale the boxes back to the original frame.
    cv::Mat resized;
    cv::resize(frame, resized, input_size_);

    cv::Mat detections;  // N×15: [x, y, w, h, 5×landmarks(x,y), score]
    try {
        net_->detect(resized, detections);
    } catch (const cv::Exception& e) {
        std::cerr << "[FaceDetector] Inference error: " << e.what() << std::endl;
        return {};
    }

    const float sx = static_cast<float>(frame.cols) / input_size_.width;
    const float sy = static_cast<float>(frame.rows) / input_size_.height;

    std::vector<FaceBox> result;
    if (detections.empty()) return result;
    result.reserve(detections.rows);
    for (int i = 0; i < detections.rows; ++i) {
        const float* r = detections.ptr<float>(i);
        FaceBox fb;
        fb.rect       = cv::Rect2f(r[0] * sx, r[1] * sy, r[2] * sx, r[3] * sy);
        fb.confidence = r[14];
        result.push_back(fb);
    }
    return result;
}

} // namespace ai
