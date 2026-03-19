// FrameAnalyzer.h
// Analysis scheduler: runs face detection + recognition at a configurable
// rate and caches results for overlay and API serving.
//
// Thread safety:
//   - Analyze() is called from the analysis thread.
//   - GetLastResults() is safe to call from any thread (mutex-protected).

#ifndef AI_FRAME_ANALYZER_H
#define AI_FRAME_ANALYZER_H

#include "FaceDetector.h"
#include "FaceRecognizer.h"
#include "FaceDatabase.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <mutex>
#include <functional>
#include <cstdint>
#include <ctime>

namespace ai {

/** @brief Analysis result for one detected face. */
struct FaceResult {
    cv::Rect    box;         ///< Bounding box in frame pixels
    float       confidence;  ///< Detection confidence
    std::string name;        ///< Recognized name, or "unknown"
    float       similarity;  ///< Recognition similarity score
    bool        recognized;  ///< True if name was matched in DB
};

/** @brief Full analysis result for one frame. */
struct AnalysisResult {
    std::vector<FaceResult> faces;   ///< All detected (and identified) faces
    int64_t                 timestamp_ms; ///< Unix time in milliseconds
    int                     frame_id;    ///< Sequential frame counter
};

/**
 * @brief Orchestrates detection + recognition at a configured rate.
 *
 * Usage:
 *   FrameAnalyzer analyzer(detector, recognizer, database);
 *   analyzer.SetAnalyzeRate(5);  // analyze 5 frames per second
 *   analyzer.SetEventCallback([](const AnalysisResult& r) { ... });
 *   analyzer.Analyze(frame);     // call every frame; respects rate limit
 */
class FrameAnalyzer {
public:
    using EventCallback = std::function<void(const AnalysisResult&)>;

    /**
     * @param detector    Loaded FaceDetector.
     * @param recognizer  Loaded FaceRecognizer (may be nullptr to skip recognition).
     * @param database    Face database (may be nullptr to skip lookup).
     */
    FrameAnalyzer(FaceDetector*    detector,
                  FaceRecognizer*  recognizer,
                  FaceDatabase*    database);

    /**
     * @brief Maximum number of analyze() calls per second.
     * Frames in between reuse the last cached result.
     */
    void SetAnalyzeRate(int fps) { analyze_fps_ = fps; }

    /**
     * @brief Register callback invoked after each analysis (on analysis thread).
     */
    void SetEventCallback(EventCallback cb) { event_cb_ = std::move(cb); }

    /**
     * @brief Process one frame. Runs AI if rate allows, otherwise returns
     *        cached result immediately.
     * @return Latest AnalysisResult.
     */
    AnalysisResult Analyze(const cv::Mat& frame);

    /** @brief Thread-safe read of last cached result. */
    AnalysisResult GetLastResult() const;

private:
    FaceDetector*   detector_;
    FaceRecognizer* recognizer_;
    FaceDatabase*   database_;

    int             analyze_fps_  = 5;
    int             frame_id_     = 0;

    mutable std::mutex   result_mutex_;
    AnalysisResult       last_result_;

    EventCallback        event_cb_;

    // Timing for rate control
    double last_analyze_time_ = 0.0;  // seconds since epoch

    AnalysisResult RunAnalysis(const cv::Mat& frame);
    static double  NowSeconds();
};

} // namespace ai

#endif // AI_FRAME_ANALYZER_H
