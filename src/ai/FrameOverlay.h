// FrameOverlay.h
// Draws face detection / recognition results on BGR frames.
// Renders bounding boxes, name labels, and confidence scores.
//
// Thread safety: Draw() is NOT thread-safe. Call from a single thread.

#ifndef AI_FRAME_OVERLAY_H
#define AI_FRAME_OVERLAY_H

#include "FrameAnalyzer.h"
#include <opencv2/opencv.hpp>

namespace ai {

/**
 * @brief Renders AI analysis annotations on a BGR frame (in-place).
 *
 * Usage:
 *   FrameOverlay overlay;
 *   overlay.Draw(frame, result);
 */
class FrameOverlay {
public:
    struct Style {
        cv::Scalar box_color_known   = {0, 255,   0};  // green
        cv::Scalar box_color_unknown = {0,   0, 255};  // red
        cv::Scalar text_color        = {255, 255, 255};
        int        box_thickness     = 2;
        double     font_scale        = 0.6;
        int        font_face         = cv::FONT_HERSHEY_SIMPLEX;
        bool       show_confidence   = true;
        bool       show_similarity   = false;
    };

    FrameOverlay();                      ///< Use default style
    explicit FrameOverlay(Style style);  ///< Use custom style

    /**
     * @brief Draw all face boxes and labels on `frame` in-place.
     * @param frame   BGR image to annotate.
     * @param result  Analysis result from FrameAnalyzer::Analyze().
     */
    void Draw(cv::Mat& frame, const AnalysisResult& result);

    Style& GetStyle() { return style_; }

private:
    Style style_;
    void DrawFace(cv::Mat& frame, const FaceResult& face);
};

} // namespace ai

#endif // AI_FRAME_OVERLAY_H
