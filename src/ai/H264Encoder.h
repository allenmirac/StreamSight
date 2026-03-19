// H264Encoder.h
// Software H.264 encoder wrapping OpenCV VideoWriter (or optionally
// a direct FFmpeg invocation).  Encodes BGR cv::Mat frames into
// Annex-B H.264 NAL data compatible with xop::AVFrame.
//
// Thread safety: NOT thread-safe. Use from a single encoding thread.

#ifndef AI_H264_ENCODER_H
#define AI_H264_ENCODER_H

#include <opencv2/opencv.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace ai {

/**
 * @brief Encodes raw BGR frames to H.264 Annex-B NAL units.
 *
 * Internally writes to a temporary pipe / memory file using the
 * OpenCV VideoWriter with the "avc1" / "X264" fourcc, then reads
 * back individual NAL units and delivers them via callback.
 *
 * For simplicity the implementation uses an in-memory pipe via
 * named pipe or temp file with FFmpeg. Each call to EncodeFrame()
 * triggers the callback synchronously (or returns the NAL data).
 *
 * Typical usage:
 *   H264Encoder enc(1280, 720, 25);
 *   enc.SetOutputCallback([](const uint8_t* nal, size_t len, bool key) {
 *       // feed into RtspServer::PushFrame
 *   });
 *   enc.Open();
 *   enc.EncodeFrame(mat);
 */
class H264Encoder {
public:
    using FrameCallback = std::function<void(const uint8_t* data,
                                             size_t len,
                                             bool   is_keyframe)>;

    /**
     * @param width    Frame width  (must match input Mat).
     * @param height   Frame height (must match input Mat).
     * @param fps      Output frame rate.
     * @param bitrate  Target bitrate in bps (default 2 Mbps).
     */
    H264Encoder(int width, int height, double fps, int bitrate = 2000000);
    ~H264Encoder();

    /**
     * @brief Register callback invoked for each encoded NAL packet.
     *  The callback is called synchronously inside EncodeFrame().
     */
    void SetOutputCallback(FrameCallback cb) { callback_ = std::move(cb); }

    /** @brief Open encoder resources. Must be called before EncodeFrame(). */
    bool Open();

    /** @brief Release encoder resources. */
    void Close();

    /**
     * @brief Encode one BGR frame.
     * @param frame  BGR image (CV_8UC3). Size must match width/height.
     * @return true on success.
     *
     * If a callback is registered, it is invoked with the encoded data.
     * Otherwise the data is silently discarded.
     */
    bool EncodeFrame(const cv::Mat& frame);

    bool IsOpened() const { return opened_; }

private:
    int           width_;
    int           height_;
    double        fps_;
    int           bitrate_;
    bool          opened_ = false;
    FrameCallback callback_;

    // FFmpeg process pipe
    FILE*         ffmpeg_pipe_ = nullptr;
    std::string   pipe_cmd_;

    // Read thread for ffmpeg stdout
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void BuildCommand();
};

} // namespace ai

#endif // AI_H264_ENCODER_H
