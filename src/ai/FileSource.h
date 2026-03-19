// FileSource.h
// VideoSource implementation for local video files (mp4, avi, H.264, etc.)
// Supports optional looping.
//
// Thread safety: single capture thread.

#ifndef AI_FILE_SOURCE_H
#define AI_FILE_SOURCE_H

#include "VideoSource.h"
#include <opencv2/opencv.hpp>
#include <string>

namespace ai {

/**
 * @brief Reads frames from a local video file.
 *
 * When loop_ is true, the file is rewound on EOF and playback restarts.
 *
 * Usage:
 *   FileSource src("video.mp4", true);
 *   src.Open();
 *   cv::Mat frame;
 *   while (src.GrabFrame(frame)) { ... }
 */
class FileSource : public VideoSource {
public:
    /**
     * @param path  Path to video file.
     * @param loop  If true, rewind and replay on EOF.
     */
    explicit FileSource(const std::string& path, bool loop = true);

    bool   Open()    override;
    void   Close()   override;
    bool   IsOpened() const override;
    bool   GrabFrame(cv::Mat& frame) override;
    double GetFPS()    const override;
    int    GetWidth()  const override;
    int    GetHeight() const override;

private:
    std::string      path_;
    bool             loop_;
    cv::VideoCapture cap_;
};

} // namespace ai

#endif // AI_FILE_SOURCE_H
