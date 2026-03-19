// VideoSource.h
// Abstract base class for all video input sources.
// Subclasses: CameraSource, FileSource, RtspPullSource.
//
// Thread safety: GrabFrame() is called from a single capture thread.
// IsOpened() may be polled from any thread.

#ifndef AI_VIDEO_SOURCE_H
#define AI_VIDEO_SOURCE_H

#include <opencv2/opencv.hpp>
#include <string>
#include <memory>

namespace ai {

/**
 * @brief Unified interface for video frame acquisition.
 *
 * All sources expose the same interface so that downstream
 * pipeline components (encoder, analyzer) are source-agnostic.
 */
class VideoSource {
public:
    virtual ~VideoSource() = default;

    /**
     * @brief Open / initialize the source.
     * @return true on success.
     */
    virtual bool Open() = 0;

    /** @brief Release underlying resources. */
    virtual void Close() = 0;

    /** @brief Whether the source is currently open. */
    virtual bool IsOpened() const = 0;

    /**
     * @brief Grab the next frame (blocking).
     * @param frame Output BGR frame.
     * @return true if a frame was produced; false on EOF / error.
     */
    virtual bool GrabFrame(cv::Mat& frame) = 0;

    /** @brief Nominal frames-per-second of the source (0 = unknown). */
    virtual double GetFPS() const { return 0.0; }

    /** @brief Frame width in pixels (0 = unknown). */
    virtual int GetWidth() const { return 0; }

    /** @brief Frame height in pixels (0 = unknown). */
    virtual int GetHeight() const { return 0; }
};

} // namespace ai

#endif // AI_VIDEO_SOURCE_H
