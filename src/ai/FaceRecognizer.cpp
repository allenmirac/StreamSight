// FaceRecognizer.cpp

#include "FaceRecognizer.h"
#include <cmath>
#include <iostream>
#include <numeric>

namespace ai {

FaceRecognizer::FaceRecognizer(const std::string& model_path)
    : model_path_(model_path)
{}

bool FaceRecognizer::Load() {
    try {
        net_ = cv::dnn::readNetFromONNX(model_path_);
    } catch (const cv::Exception& e) {
        std::cerr << "[FaceRecognizer] Cannot load " << model_path_
                  << ": " << e.what() << std::endl;
        return false;
    }
    if (net_.empty()) return false;
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    loaded_ = true;
    return true;
}

std::vector<float> FaceRecognizer::Extract(const cv::Mat& face) {
    if (!loaded_ || face.empty()) return {};

    cv::Mat resized;
    cv::resize(face, resized, cv::Size(kInputSize, kInputSize));

    // ArcFace expects BGR→RGB, normalized to [-1, 1]
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_32F, 1.0 / 127.5, -1.0);

    // Build NCHW blob
    cv::Mat blob = cv::dnn::blobFromImage(rgb);
    net_.setInput(blob);

    cv::Mat output;
    try {
        output = net_.forward();
    } catch (const cv::Exception& e) {
        std::cerr << "[FaceRecognizer] Inference error: " << e.what() << std::endl;
        return {};
    }

    if (output.empty()) return {};

    // Flatten to 1-D vector
    output = output.reshape(1, 1);
    std::vector<float> emb(output.ptr<float>(),
                            output.ptr<float>() + output.total());
    L2Normalize(emb);
    return emb;
}

float FaceRecognizer::Similarity(const std::vector<float>& a,
                                  const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return -1.0f;
    float dot = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) dot += a[i] * b[i];
    // Both are L2-normalized so ||a||=||b||=1; dot product = cosine similarity
    return dot;
}

void FaceRecognizer::L2Normalize(std::vector<float>& v) {
    float norm = 0.0f;
    for (float x : v) norm += x * x;
    norm = std::sqrt(norm);
    if (norm < 1e-10f) return;
    for (float& x : v) x /= norm;
}

} // namespace ai
