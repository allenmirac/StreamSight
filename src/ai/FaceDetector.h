// FaceDetector.h
// Face detection using the OpenCV DNN module with a YuNet ONNX model.
//
// We run the model through the generic cv::dnn API and decode the raw YuNet
// heads ourselves, instead of using cv::FaceDetectorYN. On the OpenCV 4.5.4
// shipped with Ubuntu 22.04, FaceDetectorYN targets an older YuNet anchor
// layout and its detect() fails on current zoo models with
// "Layer with requested id=-1" (see PLAN.md T1.1). Driving cv::dnn directly is
// the same approach FaceRecognizer already uses, and keeps the model usable.
//
// Thread safety: NOT thread-safe. Use from a single analysis thread.

#ifndef STREAMSIGHT_AI_FACE_DETECTOR_H
#define STREAMSIGHT_AI_FACE_DETECTOR_H

#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace streamsight::ai {

/** @brief Bounding box + confidence for one detected face. */
struct FaceBox {
  cv::Rect2f rect;   ///< Bounding box in pixel coordinates
  float confidence;  ///< Detection confidence [0, 1]
};

/**
 * @brief Detects face bounding boxes in BGR images via OpenCV DNN.
 *
 * Supported model formats: ONNX (SSD/YuFace/RetinaFace style).
 *
 * Usage:
 *   FaceDetector det("models/face_detection.onnx");
 *   det.Load();
 *   auto boxes = det.Detect(frame);
 */
class FaceDetector {
 public:
  /**
   * @param model_path   Path to ONNX detection model.
   * @param score_thresh Minimum confidence to keep (default 0.7).
   * @param nms_thresh   IoU threshold for NMS (default 0.3).
   * @param input_size   Network input size (default 320×320).
   */
  explicit FaceDetector(const std::string& model_path,
                        float score_thresh = 0.7f, float nms_thresh = 0.3f,
                        cv::Size input_size = cv::Size(320, 320));

  /** @brief Load model into DNN backend. Must call before Detect(). */
  bool Load();

  /**
   * @brief Detect all faces in a BGR frame.
   * @param frame  BGR image.
   * @return Vector of detected face boxes sorted by confidence (desc).
   */
  std::vector<FaceBox> Detect(const cv::Mat& frame);

  bool IsLoaded() const { return loaded_; }

 private:
  // Greedy non-maximum suppression over score-sorted candidates.
  static std::vector<FaceBox> Nms(const std::vector<FaceBox>& boxes,
                                  float iou_thresh);

  std::string model_path_;
  float score_thresh_;
  float nms_thresh_;
  cv::Size input_size_;
  bool loaded_ = false;
  cv::dnn::Net net_;
  std::vector<std::string> out_names_;
};

}  // namespace streamsight::ai

#endif  // STREAMSIGHT_AI_FACE_DETECTOR_H
