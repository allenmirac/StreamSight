// FrameAnalyzer.cpp

#include "FrameAnalyzer.h"
#include "observe/LatencyTracer.h"
#include <chrono>

namespace ai {

FrameAnalyzer::FrameAnalyzer(FaceDetector*   detector,
                               FaceRecognizer* recognizer,
                               FaceDatabase*   database)
    : detector_(detector)
    , recognizer_(recognizer)
    , database_(database)
{}

double FrameAnalyzer::NowSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(
               steady_clock::now().time_since_epoch()).count();
}

AnalysisResult FrameAnalyzer::Analyze(const cv::Mat& frame) {
    ++frame_id_;

    double now = NowSeconds();
    double interval = (analyze_fps_ > 0) ? (1.0 / analyze_fps_) : 0.0;

    if ((now - last_analyze_time_) >= interval) {
        last_analyze_time_ = now;
        AnalysisResult r = RunAnalysis(frame);
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            last_result_ = r;
        }
        if (event_cb_) event_cb_(r);
    }

    return GetLastResult();
}

AnalysisResult FrameAnalyzer::RunAnalysis(const cv::Mat& frame) {
    STREAMSIGHT_LATENCY_SCOPE("ai", "frame_analyze_total");

    AnalysisResult result;

    // Timestamp
    using namespace std::chrono;
    result.timestamp_ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    result.frame_id = frame_id_;

    if (!detector_) return result;

    // Detection
    std::vector<FaceBox> boxes = detector_->Detect(frame);

    // Build per-face results with default (unrecognized) values.
    std::vector<FaceResult> faces(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) {
        faces[i].box        = boxes[i].rect;
        faces[i].confidence = boxes[i].confidence;
        faces[i].name       = "unknown";
        faces[i].similarity = 0.0f;
        faces[i].recognized = false;
    }

    // Batch recognition: collect every face crop and run a single forward
    // pass over all of them, instead of one forward pass per face (the
    // per-face cost dominates the frame budget at ~64 faces/frame).
    if (recognizer_ && database_) {
        std::vector<cv::Mat> crops;
        std::vector<size_t>  crop_to_face;  // crops[i] -> faces[crop_to_face[i]]
        crops.reserve(boxes.size());
        crop_to_face.reserve(boxes.size());

        for (size_t i = 0; i < boxes.size(); ++i) {
            const cv::Rect2f& rect = boxes[i].rect;
            // Expand slightly to include forehead/chin
            cv::Rect expanded((int)rect.x, (int)rect.y,
                              (int)rect.width, (int)rect.height);
            int pad_x = (int)(rect.width  * 0.1f);
            int pad_y = (int)(rect.height * 0.15f);
            expanded.x -= pad_x;
            expanded.y -= pad_y;
            expanded.width  += 2 * pad_x;
            expanded.height += 2 * pad_y;

            // Clamp to image bounds
            expanded &= cv::Rect(0, 0, frame.cols, frame.rows);
            if (expanded.width > 0 && expanded.height > 0) {
                crops.push_back(frame(expanded));
                crop_to_face.push_back(i);
            }
        }

        if (!crops.empty()) {
            std::vector<std::vector<float>> embeddings =
                recognizer_->ExtractBatch(crops);
            for (size_t j = 0; j < crop_to_face.size() && j < embeddings.size(); ++j) {
                if (embeddings[j].empty()) continue;
                FaceMatch match = database_->Query(embeddings[j]);
                size_t face_idx = crop_to_face[j];
                faces[face_idx].name       = match.name;
                faces[face_idx].similarity = match.similarity;
                faces[face_idx].recognized = match.matched;
            }
        }
    }

    result.faces = std::move(faces);
    return result;
}

AnalysisResult FrameAnalyzer::GetLastResult() const {
    std::lock_guard<std::mutex> lock(result_mutex_);
    return last_result_;
}

} // namespace ai
