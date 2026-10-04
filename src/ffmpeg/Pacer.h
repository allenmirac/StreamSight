// Pacer.h
// Frame-rate pacing for file sources.
//
// A file demuxer returns frames as fast as the CPU allows (a 30 fps clip was
// measured at ~180 fps), which is not real-time semantics. Pacer makes the
// producing thread sleep so frames leave the pipeline at the configured rate.
// Network sources (RTSP/camera) are already real-time and leave pacing off.

#ifndef STREAMSIGHT_FFMPEG_PACER_H
#define STREAMSIGHT_FFMPEG_PACER_H

#include <chrono>
#include <cstdint>
#include <thread>

namespace streamsight::ffmpeg {

class Pacer {
public:
    // fps <= 0 disables pacing.
    void SetFps(int fps) {
        fps_ = fps;
        next_us_ = 0;
    }

    bool Enabled() const { return fps_ > 0; }

    // Block until the next frame slot is due. Call once per produced frame.
    void Wait() {
        if (fps_ <= 0) return;
        const int64_t interval = 1000000 / fps_;
        const int64_t now = NowUs();
        if (next_us_ == 0) next_us_ = now + interval;  // first call: no initial wait
        if (next_us_ > now) {
            std::this_thread::sleep_for(std::chrono::microseconds(next_us_ - now));
        } else {
            // Producer fell behind: resync instead of bursting to catch up.
            next_us_ = now;
        }
        next_us_ += interval;
    }

    // Reset the schedule, e.g. after a reconnect.
    void Reset() { next_us_ = 0; }

private:
    static int64_t NowUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    int     fps_     = 0;
    int64_t next_us_ = 0;
};

}  // namespace streamsight::ffmpeg

#endif  // STREAMSIGHT_FFMPEG_PACER_H
