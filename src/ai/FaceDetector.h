// FaceDetector.h
// Face detection using OpenCV DNN module with YuFaceDetectNet or
// a compatible SSD-style ONNX model (e.g. RetinaFace-MobileNet).
//
// Thread safety: NOT thread-safe. Use from a single analysis thread.

#ifndef AI_FACE_DETECTOR_H
#define AI_FACE_DETECTOR_H

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <vector>
#include <string>

namespace ai {

/** @brief Bounding box + confidence for one detected face. */
struct FaceBox {
    cv::Rect2f rect;      ///< Bounding box in pixel coordinates
    float      confidence; ///< Detection confidence [0, 1]
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
                          float score_thresh = 0.7f,
                          float nms_thresh   = 0.3f,
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
    std::string  model_path_;
    float        score_thresh_;
    float        nms_thresh_;
    cv::Size     input_size_;
    bool         loaded_ = false;
    cv::dnn::Net net_;

    // Post-process raw detections into FaceBox list
    std::vector<FaceBox> PostProcess(const std::vector<cv::Mat>& outs,
                                     int img_w, int img_h);
};

} // namespace ai

#endif // AI_FACE_DETECTOR_H
