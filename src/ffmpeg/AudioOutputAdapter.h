// AudioOutputAdapter.h
// Pushes decoded audio frames (PCM) to an xop::RtspServer as AAC RTP packets.
// Designed to be used alongside FFmpegStreamer's video pipeline.

#ifndef FFMPEG_AUDIO_OUTPUT_ADAPTER_H
#define FFMPEG_AUDIO_OUTPUT_ADAPTER_H

#include <cstdint>
#include <cstring>

// Forward declarations
namespace xop {
class RtspServer;
using MediaSessionId = uint32_t;
enum MediaChannelId : int;
struct AVFrame;
}

extern "C" {
#include <libavutil/frame.h>
}

namespace ffmpeg {

class AudioOutputAdapter {
public:
    AudioOutputAdapter(void* rtsp_server, uint32_t session_id,
                       int channel = 1,     // xop::channel_1
                       int sample_rate = 44100,
                       int channels = 2);

    // Push a decoded audio AVFrame (pcm_s16le or fltp) to RTSP as ADTS-wrapped AAC frames.
    // Returns true if the frame was queued successfully.
    bool PushFrame(const AVFrame* decoded);

private:
    void*    rtsp_server_   = nullptr;
    uint32_t session_id_    = 0;
    int      channel_       = 1;
    int      sample_rate_   = 44100;
    int      channels_      = 2;
};

} // namespace ffmpeg

#endif // FFMPEG_AUDIO_OUTPUT_ADAPTER_H
