// FrameDropPolicy.h
// Configuration for intelligent frame dropping under backpressure.

#ifndef FFMPEG_FRAME_DROP_POLICY_H
#define FFMPEG_FRAME_DROP_POLICY_H

#include <cstdint>

namespace ffmpeg {

struct FrameDropPolicy {
	// Maximum frame age in microseconds before a frame is considered stale.
	// When the ring buffer is full and the oldest frame exceeds this age,
	// it will be dropped to make room for a new frame.
	int64_t max_frame_age_us = 500000;  // 500 ms

	// Ring buffer fill ratio at which backpressure activates (0.0 - 1.0).
	// Below this, all frames are accepted. Above this, age-based dropping begins.
	float start_drop_ratio = 0.75f;

	// If true, drop the oldest frame when buffer is full and it's stale.
	// If false, drop the newest frame instead (keeps buffer at newest N frames).
	bool drop_oldest = true;

	// Prefer keeping I-frames (keyframes) over P-frames when deciding what to drop.
	bool prefer_keep_keyframe = true;
};

} // namespace ffmpeg

#endif // FFMPEG_FRAME_DROP_POLICY_H
