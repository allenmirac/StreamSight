// FaceDetector.cpp

#include "FaceDetector.h"

#include <iostream>

#include "FaceDetectorDetail.h"
#include "observe/LatencyTracer.h"

namespace streamsight::ai {

FaceDetector::FaceDetector(const std::string& model_path, float score_thresh,
                           float nms_thresh, cv::Size input_size)
    : model_path_(model_path),
      score_thresh_(score_thresh),
      nms_thresh_(nms_thresh),
      input_size_(input_size) {}

bool FaceDetector::Load() {
  try {
    net_ = cv::dnn::readNetFromONNX(model_path_);
  } catch (const cv::Exception& e) {
    std::cerr << "[FaceDetector] Failed to load " << model_path_ << ": "
              << e.what() << std::endl;
    return false;
  }
  if (net_.empty()) {
    std::cerr << "[FaceDetector] DNN net is empty after loading " << model_path_
              << std::endl;
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

  cv::Mat blob = cv::dnn::blobFromImage(resized, 1.0, input_size_, cv::Scalar(),
                                        /*swapRB=*/true,
                                        /*crop=*/false);
  net_.setInput(blob);

  std::vector<cv::Mat> outs;
  try {
    net_.forward(outs, out_names_);
  } catch (const cv::Exception& e) {
    std::cerr << "[FaceDetector] Inference error: " << e.what() << std::endl;
    return {};
  }

  const std::vector<FaceBox> candidates = detail::DecodeYuNet(
      outs, out_names_, input_size_, frame.size(), score_thresh_);
  return detail::Nms(candidates, nms_thresh_);
}

}  // namespace streamsight::ai
