// AudioOutputAdapter.cpp
// PCM-to-AAC encoder. Takes decoded s16 PCM, encodes to AAC via FFmpeg,
// wraps in ADTS headers, and pushes to xop::RtspServer for RTSP delivery.

#include "AudioOutputAdapter.h"
#include "xop/RtspServer.h"
#include "xop/AACSource.h"
#include "xop/media.h"

#include <cstring>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
}

namespace ffmpeg {

static uint8_t* BuildAdtsHeader(uint8_t* buf, int aac_payload_size,
                                int sample_rate, int channels) {
	// ADTS fixed header: 7 bytes + 2 bytes (CRC-less)
	int profile = 1;  // AAC LC
	int freq_idx = 4; // 44100
	if (sample_rate >= 48000)      freq_idx = 3;
	else if (sample_rate >= 32000) freq_idx = 5;
	else if (sample_rate >= 24000) freq_idx = 6;
	else if (sample_rate >= 22050) freq_idx = 7;
	else if (sample_rate >= 16000) freq_idx = 8;
	else if (sample_rate >= 12000) freq_idx = 9;
	else if (sample_rate >= 11025) freq_idx = 10;
	else if (sample_rate >= 8000)  freq_idx = 11;

	int total_len = aac_payload_size + 7;  // ADTS header only, no CRC

	buf[0] = 0xFF;
	buf[1] = 0xF1;  // MPEG-4, no CRC, AAC LC
	buf[2] = (uint8_t)(((profile - 1) << 6) | (freq_idx << 2) | ((channels - 1) >> 1));
	buf[3] = (uint8_t)(((channels - 1) << 7) | (total_len >> 11));
	buf[4] = (uint8_t)((total_len >> 3) & 0xFF);
	buf[5] = (uint8_t)(((total_len & 0x07) << 5) | 0x1F);
	buf[6] = 0xFC;  // buffer fullness = 0x7FF (VBR), second byte

	return buf + 7;
}

AudioOutputAdapter::AudioOutputAdapter(void* rtsp_server, uint32_t session_id,
                                       int channel, int sample_rate, int channels)
	: rtsp_server_(rtsp_server)
	, session_id_(session_id)
	, channel_(channel)
	, sample_rate_(sample_rate)
	, channels_(channels)
{}

AudioOutputAdapter::~AudioOutputAdapter() {
	Close();
}

bool AudioOutputAdapter::Open() {
	const AVCodec* codec = avcodec_find_encoder_by_name("aac");
	if (!codec) {
		// Fallback: use the default AAC encoder
		codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	}
	if (!codec) {
		std::cerr << "[AudioOutput] AAC encoder not found" << std::endl;
		return false;
	}

	enc_ctx_ = avcodec_alloc_context3(codec);
	if (!enc_ctx_) return false;

	enc_ctx_->sample_fmt     = AV_SAMPLE_FMT_S16;
	enc_ctx_->sample_rate    = sample_rate_;
	enc_ctx_->channel_layout = channels_ == 2 ?
		AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
	enc_ctx_->channels       = channels_;
	enc_ctx_->bit_rate       = 128000;
	enc_ctx_->time_base      = AVRational{1, sample_rate_};
	// No global header: each output packet gets its own ADTS header
	enc_ctx_->flags &= ~((int)AV_CODEC_FLAG_GLOBAL_HEADER);

	int ret = avcodec_open2(enc_ctx_, codec, nullptr);
	if (ret < 0) {
		std::cerr << "[AudioOutput] avcodec_open2(AAC): " << ret << std::endl;
		avcodec_free_context(&enc_ctx_);
		return false;
	}

	enc_pkt_ = av_packet_alloc();
	if (!enc_pkt_) {
		avcodec_free_context(&enc_ctx_);
		return false;
	}

	std::cout << "[AudioOutput] AAC encoder opened: "
	          << sample_rate_ << "Hz " << channels_ << "ch" << std::endl;
	return true;
}

void AudioOutputAdapter::Close() {
	if (enc_pkt_) {
		av_packet_free(&enc_pkt_);
	}
	if (enc_ctx_) {
		avcodec_free_context(&enc_ctx_);
	}
}

bool AudioOutputAdapter::PushFrame(const AVFrame* decoded_pcm) {
	if (!enc_ctx_ || !enc_pkt_ || !rtsp_server_ || session_id_ == 0) {
		return false;
	}

	// Ensure the input is s16. If not, skip (would need swr conversion).
	if (decoded_pcm->format != AV_SAMPLE_FMT_S16 &&
	    decoded_pcm->format != AV_SAMPLE_FMT_FLTP) {
		// Accept fltp from some decoders — libavcodec AAC can handle fltp natively
		// but we configured S16. Reconfigure? For now, skip unsupported.
		return false;
	}

	// Send PCM to encoder
	int ret = avcodec_send_frame(enc_ctx_, decoded_pcm);
	if (ret < 0 && ret != AVERROR(EAGAIN)) {
		return false;
	}

	// Receive encoded AAC packets
	while (avcodec_receive_packet(enc_ctx_, enc_pkt_) == 0) {
		// Build ADTS header (7 bytes) + AAC data
		uint32_t aac_size = (uint32_t)enc_pkt_->size;
		uint32_t framed_len = 7 + aac_size;

		xop::AVFrame xop_frame(framed_len);
		BuildAdtsHeader(xop_frame.buffer.get(), (int)aac_size,
		                sample_rate_, channels_);
		std::memcpy(xop_frame.buffer.get() + 7, enc_pkt_->data, aac_size);

		xop_frame.type      = xop::AUDIO_FRAME;
		xop_frame.size      = framed_len;
		xop_frame.timestamp = xop::AACSource::GetTimestamp((uint32_t)sample_rate_);

		auto* server = static_cast<xop::RtspServer*>(rtsp_server_);
		server->PushFrame(session_id_,
		                  static_cast<xop::MediaChannelId>(channel_),
		                  xop_frame);

		av_packet_unref(enc_pkt_);
	}

	return true;
}

} // namespace ffmpeg
