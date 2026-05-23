// AudioOutputAdapter.h
// PCM-to-AAC encoder that pushes ADTS-wrapped AAC frames to xop::RtspServer.

#ifndef FFMPEG_AUDIO_OUTPUT_ADAPTER_H
#define FFMPEG_AUDIO_OUTPUT_ADAPTER_H

#include <cstdint>
#include "xop/media.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace xop {
class RtspServer;
using MediaSessionId = uint32_t;
}

namespace ffmpeg {

class AudioOutputAdapter {
public:
	AudioOutputAdapter(void* rtsp_server, uint32_t session_id,
	                   int channel = 1,     // xop::channel_1
	                   int sample_rate = 44100,
	                   int channels = 2);

	~AudioOutputAdapter();

	// Open AAC encoder. Must be called before PushFrame.
	bool Open();

	// Close encoder and free resources.
	void Close();

	// Encode a decoded PCM AVFrame (s16 interleaved) to AAC,
	// wrap in ADTS headers, and push to RTSP via PushFrame.
	bool PushFrame(const AVFrame* decoded_pcm);

	bool IsOpened() const { return enc_ctx_ != nullptr; }

private:
	void*    rtsp_server_   = nullptr;
	uint32_t session_id_    = 0;
	int      channel_       = 1;
	int      sample_rate_   = 44100;
	int      channels_      = 2;

	AVCodecContext* enc_ctx_ = nullptr;
	AVPacket*       enc_pkt_ = nullptr;
	int64_t         samples_written_ = 0;  // for RTP timestamp
};

} // namespace ffmpeg

#endif // FFMPEG_AUDIO_OUTPUT_ADAPTER_H
