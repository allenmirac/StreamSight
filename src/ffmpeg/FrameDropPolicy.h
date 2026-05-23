// FrameDropPolicy.h
// Configuration for intelligent frame dropping under backpressure.

#ifndef FFMPEG_FRAME_DROP_POLICY_H
#define FFMPEG_FRAME_DROP_POLICY_H

#include <cstdint>

namespace ffmpeg {

struct FrameDropPolicy {
	// Maximum frame age in microseconds before a frame is considered stale.
	// Used by PushOrDrop (reactive, when buffer is full) and PruneStale
	// (proactive sliding window, when fill ratio >= start_drop_ratio).
	int64_t max_frame_age_us = 500000;  // 500 ms

	// Sliding window width in microseconds. When > 0, frames more than
	// time_window_us behind the newest frame are pruned (regardless of
	// absolute age). 0 disables relative-age pruning (only absolute age used).
	int64_t time_window_us = 0;

	// Ring buffer fill ratio at which proactive time pruning activates (0.0 - 1.0).
	// Below this, only reactive PushOrDrop is used (saves CPU).
	// Also used as the AI rate-control threshold.
	float start_drop_ratio = 0.75f;

	// Prefer keeping I-frames (keyframes) over P-frames during PruneStale.
	// Keyframes are moved to the back of the queue rather than dropped,
	// but scanning continues past them.
	bool prefer_keep_keyframe = true;
};

} // namespace ffmpeg

#endif // FFMPEG_FRAME_DROP_POLICY_H
