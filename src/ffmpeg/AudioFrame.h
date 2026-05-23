// AudioFrame.h
// Audio frame struct for cross-thread transfer in StreamPipeline audio ring buffer.

#ifndef FFMPEG_AUDIO_FRAME_H
#define FFMPEG_AUDIO_FRAME_H

#include <cstdint>
#include <memory>
#include <vector>

namespace ffmpeg {

struct AudioFrame {
	std::shared_ptr<std::vector<uint8_t>> pcm_data;  // int16 interleaved PCM
	int64_t capture_time_us = 0;
	int     sample_rate     = 44100;
	int     channels        = 2;
	int     nb_samples      = 0;

	int64_t Timestamp() const { return capture_time_us; }
};

} // namespace ffmpeg

#endif // FFMPEG_AUDIO_FRAME_H
