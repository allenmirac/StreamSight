// IOutputAdapter.h
// Abstract interface for output adapters (RTSP, RTMP, file, etc.).

#ifndef FFMPEG_I_OUTPUT_ADAPTER_H
#define FFMPEG_I_OUTPUT_ADAPTER_H

#include <string>
#include <memory>

struct AVCodecContext;
struct AVPacket;

namespace ffmpeg {

struct OutputConfig {
    std::string url;          // output URL (e.g., rtmp://server/live/stream)
    void*       rtsp_server = nullptr;  // xop::RtspServer* (for RtspOutputAdapter)
    uint32_t    session_id  = 0;        // xop::MediaSessionId
    uint32_t    fps         = 25;
    bool        enable_latency_sei = false;  // inject SEI latency marker on keyframes
};

class IOutputAdapter {
public:
    virtual ~IOutputAdapter() = default;

    // Called once after encoder is opened. enc_ctx provides codec
    // parameters (extradata, time_base) for proper setup.
    virtual bool Open(const AVCodecContext* enc_ctx,
                      const OutputConfig& cfg) = 0;

    virtual void Close() = 0;

    // Write one encoded packet. Ownership of pkt remains with caller;
    // adapter must copy if it needs to retain data beyond this call.
    virtual bool WritePacket(const AVPacket* pkt) = 0;

    virtual bool IsOpened() const = 0;
};

} // namespace ffmpeg

#endif // FFMPEG_I_OUTPUT_ADAPTER_H
