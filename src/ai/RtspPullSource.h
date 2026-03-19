// RtspPullSource.h
// VideoSource implementation that pulls frames from a remote RTSP stream
// via OpenCV VideoCapture (uses FFmpeg backend internally).
//
// Thread safety: single capture thread.

#ifndef AI_RTSP_PULL_SOURCE_H
#define AI_RTSP_PULL_SOURCE_H

#include "VideoSource.h"
#include <opencv2/opencv.hpp>
#include <string>

namespace ai {

/**
 * @brief Decodes frames from a remote RTSP / HTTP stream.
 *
 * Example:
 *   RtspPullSource src("rtsp://192.168.1.100:554/stream");
 *   src.Open();
 *   cv::Mat frame;
 *   while (src.GrabFrame(frame)) { ... }
 */
class RtspPullSource : public VideoSource {
public:
    /**
     * @param url         Full RTSP / HTTP URL.
     * @param transport   "tcp" (default) or "udp" — passed to FFmpeg via
     *                    CAP_PROP_OPEN_TIMEOUT_MSEC / environment hints.
     */
    explicit RtspPullSource(const std::string& url,
                            const std::string& transport = "tcp");

    bool   Open()    override;
    void   Close()   override;
    bool   IsOpened() const override;
    bool   GrabFrame(cv::Mat& frame) override;
    double GetFPS()    const override;
    int    GetWidth()  const override;
    int    GetHeight() const override;

private:
    std::string      url_;
    std::string      transport_;
    cv::VideoCapture cap_;
};

} // namespace ai

#endif // AI_RTSP_PULL_SOURCE_H
