// CameraSource.h
// VideoSource implementation for USB / V4L2 cameras via OpenCV VideoCapture.
//
// Thread safety: same as VideoSource — single capture thread.

#ifndef AI_CAMERA_SOURCE_H
#define AI_CAMERA_SOURCE_H

#include "VideoSource.h"
#include <opencv2/opencv.hpp>

namespace ai {

/**
 * @brief Captures frames from a local camera device (e.g. /dev/video0).
 *
 * Usage:
 *   CameraSource src(0);          // device index 0
 *   src.Open();
 *   cv::Mat frame;
 *   while (src.GrabFrame(frame)) { ... }
 */
class CameraSource : public VideoSource {
public:
    /**
     * @param device_index  V4L2 device index (0 = /dev/video0).
     * @param width         Requested capture width  (0 = camera default).
     * @param height        Requested capture height (0 = camera default).
     * @param fps           Requested capture FPS    (0 = camera default).
     */
    explicit CameraSource(int device_index = 0,
                          int width = 0, int height = 0, double fps = 0.0);

    bool   Open()    override;
    void   Close()   override;
    bool   IsOpened() const override;
    bool   GrabFrame(cv::Mat& frame) override;
    double GetFPS()    const override;
    int    GetWidth()  const override;
    int    GetHeight() const override;

private:
    int          device_index_;
    int          req_width_;
    int          req_height_;
    double       req_fps_;
    cv::VideoCapture cap_;
};

} // namespace ai

#endif // AI_CAMERA_SOURCE_H
