// RtspOutputAdapter.h
// Output adapter that pushes encoded NAL units to xop::RtspServer.

#ifndef FFMPEG_RTSP_OUTPUT_ADAPTER_H
#define FFMPEG_RTSP_OUTPUT_ADAPTER_H

#include "IOutputAdapter.h"
#include <string>
#include <cstdint>

// Forward declarations (types used via void* in this header)
namespace xop {
class RtspServer;
using MediaSessionId = uint32_t;
} // namespace xop

namespace ffmpeg {

class RtspOutputAdapter : public IOutputAdapter {
public:
    RtspOutputAdapter(void* rtsp_server,    // xop::RtspServer*
                      uint32_t session_id,  // xop::MediaSessionId
                      int channel = 0);     // xop::MediaChannelId

    bool Open(const AVCodecContext* enc_ctx,
              const OutputConfig& cfg) override;
    void Close() override;
    bool WritePacket(const AVPacket* pkt) override;
    bool IsOpened() const override;

private:
    void*    rtsp_server_   = nullptr;  // xop::RtspServer*
    uint32_t session_id_    = 0;
    int      channel_       = 0;
    int      fps_           = 25;
};

} // namespace ffmpeg

#endif // FFMPEG_RTSP_OUTPUT_ADAPTER_H
