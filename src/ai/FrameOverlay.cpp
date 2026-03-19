// FrameOverlay.cpp

#include "FrameOverlay.h"
#include <sstream>
#include <iomanip>

namespace ai {

FrameOverlay::FrameOverlay() {}
FrameOverlay::FrameOverlay(Style style) : style_(std::move(style)) {}

void FrameOverlay::Draw(cv::Mat& frame, const AnalysisResult& result) {
    for (const auto& face : result.faces) {
        DrawFace(frame, face);
    }

    // Frame info overlay (top-left)
    std::ostringstream info;
    info << "Faces: " << result.faces.size();
    cv::putText(frame, info.str(),
                cv::Point(10, 25),
                style_.font_face, style_.font_scale,
                cv::Scalar(0, 200, 200), 1, cv::LINE_AA);
}

void FrameOverlay::DrawFace(cv::Mat& frame, const FaceResult& face) {
    cv::Scalar color = face.recognized
                       ? style_.box_color_known
                       : style_.box_color_unknown;

    // Bounding box
    cv::rectangle(frame, face.box, color, style_.box_thickness, cv::LINE_AA);

    // Label text
    std::string label = face.name;
    if (style_.show_confidence) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << face.confidence;
        label += " (" + ss.str() + ")";
    }
    if (style_.show_similarity && face.recognized) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << face.similarity;
        label += " [" + ss.str() + "]";
    }

    // Background rect for text legibility
    int base;
    cv::Size ts = cv::getTextSize(label, style_.font_face,
                                   style_.font_scale, 1, &base);
    cv::Point tl(face.box.x, face.box.y - ts.height - 6);
    if (tl.y < 0) tl.y = face.box.y + ts.height + 6;

    cv::rectangle(frame,
                  cv::Point(tl.x, tl.y - ts.height - 2),
                  cv::Point(tl.x + ts.width + 2, tl.y + base),
                  color, cv::FILLED);

    cv::putText(frame, label,
                cv::Point(tl.x + 1, tl.y - 1),
                style_.font_face, style_.font_scale,
                style_.text_color, 1, cv::LINE_AA);
}

} // namespace ai
