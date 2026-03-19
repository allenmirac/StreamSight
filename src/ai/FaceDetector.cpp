// FaceDetector.cpp

#include "FaceDetector.h"
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
        net_ = cv::dnn::readNetFromONNX(model_path_);
    } catch (const cv::Exception& e) {
        std::cerr << "[FaceDetector] Failed to load " << model_path_
                  << ": " << e.what() << std::endl;
        return false;
    }
    if (net_.empty()) {
        std::cerr << "[FaceDetector] Network is empty after loading." << std::endl;
        return false;
    }
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    loaded_ = true;
    return true;
}

std::vector<FaceBox> FaceDetector::Detect(const cv::Mat& frame) {
    if (!loaded_ || frame.empty()) return {};

    // Build blob: 1×3×H×W, mean subtracted, BGR order
    cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0,
                                          input_size_,
                                          cv::Scalar(104, 117, 123),
                                          /*swapRB=*/false,
                                          /*crop=*/false);
    net_.setInput(blob);

    std::vector<cv::Mat> outs;
    try {
        net_.forward(outs, net_.getUnconnectedOutLayersNames());
    } catch (const cv::Exception& e) {
        std::cerr << "[FaceDetector] Inference error: " << e.what() << std::endl;
        return {};
    }

    return PostProcess(outs, frame.cols, frame.rows);
}

std::vector<FaceBox> FaceDetector::PostProcess(
        const std::vector<cv::Mat>& outs, int img_w, int img_h) {
    std::vector<FaceBox>  boxes;
    std::vector<float>    confidences;
    std::vector<cv::Rect> rects;

    for (const cv::Mat& out : outs) {
        // Expected shape: [1, N, 7] where each row is
        // [_, class_id, conf, x1, y1, x2, y2] (SSD format)
        // or [N, 15] (SCRFD/RetinaFace: x1,y1,x2,y2,conf, landmarks...)
        // We handle both by checking the column count.
        const float* data = (const float*)out.data;
        int rows = out.total() / (out.cols > 0 ? out.cols : 7);
        int cols = out.cols > 0 ? out.cols : 7;

        for (int i = 0; i < rows; ++i) {
            const float* row = data + i * cols;
            float conf;
            float x1, y1, x2, y2;

            if (cols == 7) {
                // SSD-style: [image_id, class_id, conf, x1, y1, x2, y2]
                conf = row[2];
                x1 = row[3] * img_w;
                y1 = row[4] * img_h;
                x2 = row[5] * img_w;
                y2 = row[6] * img_h;
            } else if (cols >= 5) {
                // RetinaFace/SCRFD style: [x1, y1, x2, y2, conf, ...]
                x1 = row[0]; y1 = row[1];
                x2 = row[2]; y2 = row[3];
                conf = row[4];
                // Check if coords are normalized [0,1]
                if (x2 <= 1.0f) {
                    x1 *= img_w; y1 *= img_h;
                    x2 *= img_w; y2 *= img_h;
                }
            } else {
                continue;
            }

            if (conf < score_thresh_) continue;

            int lx = std::max(0, (int)x1);
            int ly = std::max(0, (int)y1);
            int w  = std::min(img_w, (int)x2) - lx;
            int h  = std::min(img_h, (int)y2) - ly;
            if (w <= 0 || h <= 0) continue;

            confidences.push_back(conf);
            rects.emplace_back(lx, ly, w, h);
        }
    }

    // NMS
    std::vector<int> indices;
    cv::dnn::NMSBoxes(rects, confidences, score_thresh_, nms_thresh_, indices);

    std::vector<FaceBox> result;
    result.reserve(indices.size());
    for (int idx : indices) {
        FaceBox fb;
        fb.rect       = rects[idx];
        fb.confidence = confidences[idx];
        result.push_back(fb);
    }

    // Sort by confidence descending
    std::sort(result.begin(), result.end(),
              [](const FaceBox& a, const FaceBox& b) {
                  return a.confidence > b.confidence;
              });
    return result;
}

} // namespace ai
