// RtspOutputAdapter.cpp
// Pushes encoded H.264 NAL units to xop::RtspServer::PushFrame.
// Matches the exact data format produced by the existing H264Encoder::DeliverNAL
// for backward compatibility with H264Source::HandleFrame FU-A fragmentation.

#include "RtspOutputAdapter.h"
#include "xop/RtspServer.h"
#include "xop/H264Source.h"
#include "xop/media.h"
#include <cstring>
#include <cstdio>
#include <iostream>

extern "C" {
#include <libavcodec/packet.h>
}

namespace ffmpeg {

RtspOutputAdapter::RtspOutputAdapter(void* rtsp_server,
                                     uint32_t session_id,
                                     int channel)
    : rtsp_server_(rtsp_server)
    , session_id_(session_id)
    , channel_(channel)
{}

bool RtspOutputAdapter::Open(const AVCodecContext* enc_ctx,
                              const OutputConfig& cfg) {
    (void)enc_ctx;
    fps_ = (int)cfg.fps;
    return rtsp_server_ != nullptr && session_id_ > 0;
}

void RtspOutputAdapter::Close() {
    rtsp_server_ = nullptr;
    session_id_  = 0;
}

bool RtspOutputAdapter::WritePacket(const AVPacket* pkt) {
    if (!rtsp_server_ || session_id_ == 0) return false;
    if (pkt->size <= 0) return true;  // skip empty packets

    auto* server = static_cast<xop::RtspServer*>(rtsp_server_);

    // FFmpeg libx264 encoder outputs raw NAL units (no start code).
    // The existing H264Source::HandleFrame expects a 4-byte Annex-B start
    // code [0,0,0,1] prepended, matching H264Encoder::DeliverNAL behavior.
    // For FU-A fragmentation (>1420 bytes), H264Source skips 1 byte
    // (frame_buf += 1), so frame_buf[0] becomes the second byte of the
    // start code (0x00). We match this exactly for backward compat.

    static const uint8_t kStart[4] = {0, 0, 0, 1};
    uint32_t framed_len = 4 + (uint32_t)pkt->size;

    xop::AVFrame frame(framed_len);
    frame.buffer.get()[0] = kStart[0];
    frame.buffer.get()[1] = kStart[1];
    frame.buffer.get()[2] = kStart[2];
    frame.buffer.get()[3] = kStart[3];
    std::memcpy(frame.buffer.get() + 4, pkt->data, (size_t)pkt->size);

    // Determine keyframe from packet flags and NAL unit type
    uint8_t nal_type = pkt->data[0] & 0x1F;
    bool is_key = (pkt->flags & AV_PKT_FLAG_KEY) ||
                  (nal_type == 5 || nal_type == 7 || nal_type == 8);
    frame.type = is_key ? xop::VIDEO_FRAME_I : xop::VIDEO_FRAME_P;

    // Use wall-clock 90kHz timestamp (matches existing behavior)
    frame.timestamp = xop::H264Source::GetTimestamp();
    frame.size = framed_len;

    return server->PushFrame(session_id_,
                             static_cast<xop::MediaChannelId>(channel_),
                             frame);
}

bool RtspOutputAdapter::IsOpened() const {
    return rtsp_server_ != nullptr && session_id_ > 0;
}

} // namespace ffmpeg
