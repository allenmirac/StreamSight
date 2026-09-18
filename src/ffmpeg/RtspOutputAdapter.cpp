// RtspOutputAdapter.cpp
// Pushes encoded H.264 NAL units to streamsight::rtsp::RtspServer::PushFrame.
// Emits each NAL unit as a 4-byte Annex-B start code [0,0,0,1] followed by the
// payload, which is the format H264Source::HandleFrame expects for FU-A
// fragmentation.

#include "RtspOutputAdapter.h"
#include "rtsp/RtspServer.h"
#include "rtsp/H264Source.h"
#include "rtsp/media.h"
#include "rtsp/SeiLatencyMarker.h"
#include <cstring>
#include <cstdio>
#include <iostream>
#include <chrono>

extern "C" {
#include <libavcodec/packet.h>
}

namespace streamsight::ffmpeg {

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
	enable_latency_sei_ = cfg.enable_latency_sei;
	return rtsp_server_ != nullptr && session_id_ > 0;
}

void RtspOutputAdapter::Close() {
	// Identity (rtsp_server_, session_id_) set at construction survives
	// Close/Open cycles for pipeline reconnect. Only reset runtime state.
	frame_count_ = 0;
	fps_ = 0;
	enable_latency_sei_ = false;
}

bool RtspOutputAdapter::WritePacket(const AVPacket* pkt) {
	if (!rtsp_server_ || session_id_ == 0) return false;
	if (pkt->size <= 0) return true;  // skip empty packets

	auto* server = static_cast<streamsight::rtsp::RtspServer*>(rtsp_server_);

	// FFmpeg libx264 encoder outputs raw NAL units (no start code).
	// H264Source::HandleFrame expects a 4-byte Annex-B start code
	// [0,0,0,1] prepended. For FU-A fragmentation (>1420 bytes),
	// H264Source skips 1 byte (frame_buf += 1), so frame_buf[0] becomes the
	// second byte of the start code (0x00). We produce exactly that layout.

	static const uint8_t kStart[4] = {0, 0, 0, 1};

	// Determine keyframe from packet flags and NAL unit type
	uint8_t nal_type = pkt->data[0] & 0x1F;
	bool is_key = (pkt->flags & AV_PKT_FLAG_KEY) ||
	              (nal_type == 5 || nal_type == 7 || nal_type == 8);

	// Inject SEI latency marker before keyframes when enabled
	if (enable_latency_sei_ && is_key) {
		uint32_t encode_us = 0;  // encoding time not separately tracked yet
		uint64_t send_time_us = streamsight::rtsp::GetSendTimeUs();
		auto sei_nalu = streamsight::rtsp::BuildLatencySeiNalu(
		    (uint64_t)frame_count_, send_time_us, encode_us);

		streamsight::rtsp::AVFrame sei_frame((uint32_t)sei_nalu.size());
		std::memcpy(sei_frame.buffer.get(), sei_nalu.data(), sei_nalu.size());
		sei_frame.type = streamsight::rtsp::VIDEO_FRAME_I;
		sei_frame.timestamp = streamsight::rtsp::H264Source::GetTimestamp();
		sei_frame.size = (uint32_t)sei_nalu.size();
		server->PushFrame(session_id_,
		                  static_cast<streamsight::rtsp::MediaChannelId>(channel_),
		                  sei_frame);
	}

	uint32_t framed_len = 4 + (uint32_t)pkt->size;

	streamsight::rtsp::AVFrame frame(framed_len);
	frame.buffer.get()[0] = kStart[0];
	frame.buffer.get()[1] = kStart[1];
	frame.buffer.get()[2] = kStart[2];
	frame.buffer.get()[3] = kStart[3];
	std::memcpy(frame.buffer.get() + 4, pkt->data, (size_t)pkt->size);

	frame.type = is_key ? streamsight::rtsp::VIDEO_FRAME_I : streamsight::rtsp::VIDEO_FRAME_P;

	// Use wall-clock 90kHz timestamp (matches existing behavior)
	frame.timestamp = streamsight::rtsp::H264Source::GetTimestamp();
	frame.size = framed_len;

	++frame_count_;

	return server->PushFrame(session_id_,
	                         static_cast<streamsight::rtsp::MediaChannelId>(channel_),
	                         frame);
}

bool RtspOutputAdapter::IsOpened() const {
	return rtsp_server_ != nullptr && session_id_ > 0;
}

}  // namespace streamsight::ffmpeg
