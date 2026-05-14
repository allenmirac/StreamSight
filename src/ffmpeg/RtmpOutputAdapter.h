// RtmpOutputAdapter.h
// Output adapter that muxes encoded packets to RTMP via FFmpeg muxer.

#ifndef FFMPEG_RTMP_OUTPUT_ADAPTER_H
#define FFMPEG_RTMP_OUTPUT_ADAPTER_H

#include "IOutputAdapter.h"
#include <string>

struct AVFormatContext;
struct AVStream;

namespace ffmpeg {

class RtmpOutputAdapter : public IOutputAdapter {
public:
    explicit RtmpOutputAdapter(const std::string& rtmp_url);
    ~RtmpOutputAdapter() override;

    bool Open(const AVCodecContext* enc_ctx,
              const OutputConfig& cfg) override;
    void Close() override;
    bool WritePacket(const AVPacket* pkt) override;
    bool IsOpened() const override;

private:
    std::string       rtmp_url_;
    AVFormatContext*  ofmt_ctx_       = nullptr;
    AVStream*         video_stream_   = nullptr;
    int64_t           pts_counter_    = 0;
    bool              opened_         = false;
};

} // namespace ffmpeg

#endif // FFMPEG_RTMP_OUTPUT_ADAPTER_H
