// FrameAnalyzer.cpp

#include "FrameAnalyzer.h"
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
    AnalysisResult result;

    // Timestamp
    using namespace std::chrono;
    result.timestamp_ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    result.frame_id = frame_id_;

    if (!detector_) return result;

    // Detection
    std::vector<FaceBox> boxes = detector_->Detect(frame);

    for (const auto& fb : boxes) {
        FaceResult fr;
        fr.box        = fb.rect;
        fr.confidence = fb.confidence;
        fr.name       = "unknown";
        fr.similarity = 0.0f;
        fr.recognized = false;

        // Recognition (if enabled)
        if (recognizer_ && database_) {
            // Expand rect slightly to include forehead/chin
            cv::Rect expanded = fb.rect;
            int pad_x = (int)(fb.rect.width  * 0.1f);
            int pad_y = (int)(fb.rect.height * 0.15f);
            expanded.x -= pad_x; expanded.y -= pad_y;
            expanded.width  += 2 * pad_x;
            expanded.height += 2 * pad_y;

            // Clamp to image bounds
            expanded &= cv::Rect(0, 0, frame.cols, frame.rows);
            if (expanded.width > 0 && expanded.height > 0) {
                cv::Mat crop = frame(expanded);
                std::vector<float> emb = recognizer_->Extract(crop);
                if (!emb.empty()) {
                    FaceMatch match = database_->Query(emb);
                    fr.name       = match.name;
                    fr.similarity = match.similarity;
                    fr.recognized = match.matched;
                }
            }
        }

        result.faces.push_back(fr);
    }

    return result;
}

AnalysisResult FrameAnalyzer::GetLastResult() const {
    std::lock_guard<std::mutex> lock(result_mutex_);
    return last_result_;
}

} // namespace ai
