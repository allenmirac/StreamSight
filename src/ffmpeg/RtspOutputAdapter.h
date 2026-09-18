// RtspOutputAdapter.h
// Output adapter that pushes encoded NAL units to streamsight::rtsp::RtspServer.

#ifndef STREAMSIGHT_FFMPEG_RTSP_OUTPUT_ADAPTER_H
#define STREAMSIGHT_FFMPEG_RTSP_OUTPUT_ADAPTER_H

#include "IOutputAdapter.h"
#include <string>
#include <cstdint>

// Forward declarations (types used via void* in this header)
namespace streamsight::rtsp {
class RtspServer;
using MediaSessionId = uint32_t;
}  // namespace streamsight::rtsp

namespace streamsight::ffmpeg {

class RtspOutputAdapter : public IOutputAdapter {
public:
	RtspOutputAdapter(void* rtsp_server,    // streamsight::rtsp::RtspServer*
	                  uint32_t session_id,  // streamsight::rtsp::MediaSessionId
	                  int channel = 0);     // streamsight::rtsp::MediaChannelId

	bool Open(const AVCodecContext* enc_ctx,
	          const OutputConfig& cfg) override;
	void Close() override;
	bool WritePacket(const AVPacket* pkt) override;
	bool IsOpened() const override;

private:
	void*    rtsp_server_   = nullptr;  // streamsight::rtsp::RtspServer*
	uint32_t session_id_    = 0;
	int      channel_       = 0;
	int      fps_           = 25;
	bool     enable_latency_sei_ = false;
	int64_t  frame_count_   = 0;       // for SEI frame_id
};

}  // namespace streamsight::ffmpeg

#endif // STREAMSIGHT_FFMPEG_RTSP_OUTPUT_ADAPTER_H
