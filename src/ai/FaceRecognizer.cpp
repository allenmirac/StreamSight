// FaceRecognizer.cpp

#include "FaceRecognizer.h"
#include "observe/LatencyTracer.h"
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
    std::vector<std::vector<float>> batch =
        ExtractBatch(std::vector<cv::Mat>{face});
    return batch.empty() ? std::vector<float>() : std::move(batch[0]);
}

std::vector<std::vector<float>> FaceRecognizer::ExtractBatch(
        const std::vector<cv::Mat>& faces) {
    STREAMSIGHT_LATENCY_SCOPE("ai", "face_recognition");
    std::vector<std::vector<float>> embeddings(faces.size());
    if (!loaded_ || faces.empty()) return embeddings;

    // Preprocess every crop (resize + BGR→RGB + normalize to [-1, 1]).
    // Empty inputs are skipped and map to an empty embedding.
    std::vector<cv::Mat> preprocessed;
    std::vector<size_t> index_map;  // preprocessed[i] came from faces[index_map[i]]
    preprocessed.reserve(faces.size());
    index_map.reserve(faces.size());
    for (size_t i = 0; i < faces.size(); ++i) {
        if (faces[i].empty()) continue;

        cv::Mat resized;
        cv::resize(faces[i], resized, cv::Size(kInputSize, kInputSize));

        cv::Mat rgb;
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
        rgb.convertTo(rgb, CV_32F, 1.0 / 127.5, -1.0);

        preprocessed.push_back(rgb);
        index_map.push_back(i);
    }
    if (preprocessed.empty()) return embeddings;

    // Single batched forward pass over N×3×112×112.
    cv::Mat blob = cv::dnn::blobFromImages(preprocessed);
    net_.setInput(blob);

    cv::Mat output;
    try {
        output = net_.forward();
    } catch (const cv::Exception& e) {
        std::cerr << "[FaceRecognizer] Inference error: " << e.what() << std::endl;
        return embeddings;
    }
    if (output.empty()) return embeddings;

    // Model output is N×dim (or 1×(N·dim)); reshape to N rows and slice.
    const size_t n = preprocessed.size();
    if (output.total() < n || output.total() % n != 0) {
        std::cerr << "[FaceRecognizer] Unexpected output shape for batch=" << n
                  << " (total=" << output.total() << ")" << std::endl;
        return embeddings;
    }
    cv::Mat rows = output.reshape(1, static_cast<int>(n));
    for (size_t b = 0; b < n; ++b) {
        cv::Mat row = rows.row(b);
        std::vector<float> emb(row.ptr<float>(),
                                row.ptr<float>() + row.total());
        L2Normalize(emb);
        embeddings[index_map[b]] = std::move(emb);
    }
    return embeddings;
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
