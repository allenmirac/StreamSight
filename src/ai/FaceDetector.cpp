// FaceDetector.cpp

#include "FaceDetector.h"
#include "observe/LatencyTracer.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace streamsight::ai {

namespace {

// YuNet emits 4 tensors per feature level: classification, objectness,
// box regression and 5-point landmarks.
struct LevelHeads {
    int cls  = -1;
    int obj  = -1;
    int bbox = -1;
    int kps  = -1;
};

// Per-level anchor width as a fraction of the network input, keyed by stride.
// YuNet's ONNX export does not document its anchor sizes and the newest zoo
// models no longer match the multi-anchor layout OpenCV 4.5.4 hardcodes, so
// these were calibrated empirically against labelled frames (see PLAN.md T1.1):
// the box width is `prior * expf(0.1 * dw) * input`, and this prior is the
// value that reproduces the true face box across a range of face scales.
float PriorForStride(int stride) {
    switch (stride) {
        case 8:  return 1.0f / 3.0f;
        case 16: return 0.5f;
        case 32: return 3.0f / 4.0f;
        default: return 0.5f;
    }
}

}  // namespace

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
        net_ = cv::dnn::readNetFromONNX(model_path_);
    } catch (const cv::Exception& e) {
        std::cerr << "[FaceDetector] Failed to load " << model_path_
                  << ": " << e.what() << std::endl;
        return false;
    }
    if (net_.empty()) {
        std::cerr << "[FaceDetector] DNN net is empty after loading "
                  << model_path_ << std::endl;
        return false;
    }
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

    out_names_ = net_.getUnconnectedOutLayersNames();
    if (out_names_.empty()) {
        std::cerr << "[FaceDetector] Model exposes no output layers." << std::endl;
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

    cv::Mat blob = cv::dnn::blobFromImage(
        resized, 1.0, input_size_, cv::Scalar(), /*swapRB=*/true,
        /*crop=*/false);
    net_.setInput(blob);

    std::vector<cv::Mat> outs;
    try {
        net_.forward(outs, out_names_);
    } catch (const cv::Exception& e) {
        std::cerr << "[FaceDetector] Inference error: " << e.what() << std::endl;
        return {};
    }

    // Resolve each head by name so we do not depend on output ordering.
    auto find = [this](const std::string& name) -> int {
        for (size_t i = 0; i < out_names_.size(); ++i)
            if (out_names_[i] == name) return static_cast<int>(i);
        return -1;
    };

    const float sx = static_cast<float>(frame.cols) / input_size_.width;
    const float sy = static_cast<float>(frame.rows) / input_size_.height;

    std::vector<FaceBox> candidates;
    for (int stride : {8, 16, 32}) {
        const std::string s = std::to_string(stride);
        LevelHeads h{find("cls_" + s), find("obj_" + s),
                     find("bbox_" + s), find("kps_" + s)};
        if (h.cls < 0 || h.obj < 0 || h.bbox < 0) continue;  // head absent
        if (h.cls >= (int)outs.size() || h.obj >= (int)outs.size() ||
            h.bbox >= (int)outs.size()) continue;

        const int fw = input_size_.width / stride;
        const int fh = input_size_.height / stride;
        const int n  = fw * fh;

        cv::Mat cls  = outs[h.cls].reshape(1, n);   // (n, 1)
        cv::Mat obj  = outs[h.obj].reshape(1, n);   // (n, 1)
        cv::Mat bbox = outs[h.bbox].reshape(1, n);  // (n, 4)
        if (bbox.cols < 4) continue;

        const float prior = PriorForStride(stride);

        for (int i = 0; i < n; ++i) {
            // Raw scores are already in [0, 1]; YuNet fuses them with a sqrt.
            const float score =
                std::sqrt(cls.ptr<float>(i)[0] * obj.ptr<float>(i)[0]);
            if (score < score_thresh_) continue;

            const float cx_prior = (i % fw + 0.5f) / fw;
            const float cy_prior = (i / fw + 0.5f) / fh;
            const float* b = bbox.ptr<float>(i);

            const float cx = (cx_prior + b[0] * 0.1f * prior) * input_size_.width;
            const float cy = (cy_prior + b[1] * 0.1f * prior) * input_size_.height;
            const float w  = prior * std::exp(b[2] * 0.1f) * input_size_.width;
            const float h  = prior * std::exp(b[3] * 0.2f) * input_size_.height;

            FaceBox fb;
            fb.rect = cv::Rect2f((cx - w / 2) * sx, (cy - h / 2) * sy,
                                 w * sx, h * sy);
            fb.confidence = score;
            candidates.push_back(fb);
        }
    }

    return Nms(candidates, nms_thresh_);
}

std::vector<FaceBox> FaceDetector::Nms(const std::vector<FaceBox>& boxes,
                                       float iou_thresh) {
    std::vector<FaceBox> sorted = boxes;
    std::sort(sorted.begin(), sorted.end(),
              [](const FaceBox& a, const FaceBox& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<FaceBox> keep;
    for (const auto& cand : sorted) {
        bool suppressed = false;
        for (const auto& k : keep) {
            const float inter = (cand.rect & k.rect).area();
            const float uni   = cand.rect.area() + k.rect.area() - inter;
            if (uni > 0 && inter / uni > iou_thresh) { suppressed = true; break; }
        }
        if (!suppressed) keep.push_back(cand);
    }
    return keep;
}

}  // namespace streamsight::ai
