// FaceRecognizer.h
// Face feature extraction using ArcFace (ONNX) via OpenCV DNN.
// Produces a 512-dimensional L2-normalized embedding for each face crop.
//
// Thread safety: NOT thread-safe. Use from a single analysis thread.

#ifndef AI_FACE_RECOGNIZER_H
#define AI_FACE_RECOGNIZER_H

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <vector>
#include <string>

namespace ai {

/**
 * @brief Extracts a 512-D L2-normalized face embedding via ArcFace.
 *
 * Usage:
 *   FaceRecognizer rec("models/face_recognition.onnx");
 *   rec.Load();
 *   auto embedding = rec.Extract(face_crop);
 */
class FaceRecognizer {
public:
    /** Input size expected by ArcFace: 112×112 */
    static constexpr int kInputSize = 112;

    /**
     * @param model_path  Path to ArcFace ONNX model.
     */
    explicit FaceRecognizer(const std::string& model_path);

    /** @brief Load model. Must call before Extract(). */
    bool Load();

    /**
     * @brief Extract face embedding from a BGR face-crop.
     * @param face  BGR face image (any size; will be resized to 112×112).
     * @return 512-D float vector (L2-normalized), or empty on failure.
     */
    std::vector<float> Extract(const cv::Mat& face);

    /**
     * @brief Cosine similarity between two L2-normalized embeddings.
     * @return Similarity in [-1, 1]; typically 0.3–0.5 is same person.
     */
    static float Similarity(const std::vector<float>& a,
                             const std::vector<float>& b);

    bool IsLoaded() const { return loaded_; }

private:
    std::string  model_path_;
    bool         loaded_ = false;
    cv::dnn::Net net_;

    static void L2Normalize(std::vector<float>& v);
};

} // namespace ai

#endif // AI_FACE_RECOGNIZER_H
