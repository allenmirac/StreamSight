// StreamPipeline.h
// SRS-inspired 3-stage per-stream pipeline with thread isolation and RingBuffers.
//
// Pipeline:  [Demux+Decode] → RingBuffer → [AI Process] → RingBuffer → [Encode+Output]
//
// Each stage runs in its own thread. Stages are connected by RingBuffers with
// backpressure-driven PushOrDrop, preventing unbounded queue growth.

#ifndef FFMPEG_STREAM_PIPELINE_H
#define FFMPEG_STREAM_PIPELINE_H

#include "AudioFrame.h"
#include "FFmpegUtils.h"
#include "FrameDropPolicy.h"
#include "IOutputAdapter.h"
#include "net/RingBuffer.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
}

namespace ffmpeg {

// FrameInterceptor type (also defined in FFmpegStreamer.h)
using FrameInterceptor = std::function<bool(FFmpegFrame& frame)>;

// Decoded frame with pixel data ready for AI processing.
// Owns a copy of the BGR24 pixel buffer for safe cross-thread transfer.
struct DecodedFrame {
	std::shared_ptr<std::vector<uint8_t>> pixel_data;  // BGR24
	int      width           = 0;
	int      height          = 0;
	int      linesize        = 0;
	int64_t  frame_index     = 0;
	int64_t  capture_time_us = 0;
	bool     is_keyframe     = false;

	int64_t Timestamp() const { return capture_time_us; }
};

// Processed frame after AI overlay, ready for encoding.
// Owns the BGR24 pixel buffer with overlay applied.
struct ProcessedFrame {
	std::shared_ptr<std::vector<uint8_t>> pixel_data;  // BGR24 with overlay
	int      width           = 0;
	int      height          = 0;
	int      linesize        = 0;
	int64_t  frame_index     = 0;
	int64_t  capture_time_us = 0;
	bool     is_keyframe     = false;

	int64_t Timestamp() const { return capture_time_us; }
};

struct PipelineConfig {
	// ── Input ───────────────────────────────────────────────
	std::string input_url;
	int         open_timeout_ms    = 5000;
	int         read_timeout_ms    = 3000;
	bool        reconnect_on_eof   = false;
	int         max_reconnect      = 10;
	int         reconnect_delay_ms = 2000;

	// ── Processing ──────────────────────────────────────────
	int              output_width  = 0;
	int              output_height = 0;
	FrameInterceptor frame_cb;           // AI analysis + overlay callback

	// ── Encoder ─────────────────────────────────────────────
	std::string codec_name = "libx264";
	std::string preset     = "ultrafast";
	std::string tune       = "zerolatency";
	int         bitrate    = 2000000;
	int         fps        = 25;
	int         gop_size   = 0;
	int         threads    = 2;

	// ── Ring buffer sizes ───────────────────────────────────
	int decode_ring_size  = 4;   // demux → AI
	int process_ring_size = 4;   // AI → encode

	// ── Backpressure ────────────────────────────────────────
	FrameDropPolicy drop_policy;
	bool enable_backpressure = true;

	// ── Output ──────────────────────────────────────────────
	std::vector<std::shared_ptr<IOutputAdapter>> outputs;

	// ── Audio ────────────────────────────────────────────────
	bool enable_audio       = true;
	int  audio_sample_rate  = 44100;
	int  audio_channels     = 2;
	int  audio_ring_size    = 8;    // audio buffer: more slots, smaller frames

	// Audio output adapter (created externally, owned by caller).
	// If set, the audio path is enabled in StreamPipeline.
	void* audio_rtsp_server  = nullptr;
	uint32_t audio_session_id = 0;
	int  audio_channel       = 1;    // xop::channel_1

	// ── Client-aware pipeline gating ────────────────────────────
	// Shared with StreamSession. DemuxDecodeLoop checks these before
	// each ReadAndDecodeOnce so the pipeline pauses when no RTSP
	// clients are connected.
	std::atomic<int>*        has_clients  = nullptr;
	std::condition_variable* client_cv    = nullptr;
	std::mutex*              client_mutex = nullptr;
};

class StreamPipeline {
public:
	explicit StreamPipeline(const std::string& stream_id,
	                        const PipelineConfig& cfg);
	~StreamPipeline();

	// Lifecycle
	bool Start();
	void Stop();
	bool IsRunning() const { return running_; }

	// Stats
	int  DecodeRingFill() const;
	int  ProcessRingFill() const;
	int64_t FramesDecoded()    const { return frames_decoded_; }
	int64_t FramesDroppedDemux()  const { return dropped_demux_; }
	int64_t FramesDroppedAI()    const { return dropped_ai_; }
	int64_t FramesPrunedDemux()  const { return pruned_demux_; }
	int64_t FramesPrunedAI()     const { return pruned_ai_; }
	int64_t AudioFramesDropped() const { return audio_dropped_; }
	int64_t AudioFramesPruned()  const { return audio_pruned_; }

	int  MaxDecodeRingFill()  const { return max_decode_ring_fill_; }
	int  MaxProcessRingFill() const { return max_process_ring_fill_; }
	int64_t BackpressureEvents() const { return backpressure_events_; }

	const std::string& StreamId() const { return stream_id_; }

private:
	// Stage threads
	void DemuxDecodeLoop();
	void AIProcessLoop();
	void EncodeOutputLoop();

	// FFmpeg helpers (open/close from the demux thread)
	bool OpenDemuxer();
	void CloseDemuxer();
	bool ReadAndDecodeOnce(AVFrame* decoded);

	std::string      stream_id_;
	PipelineConfig   cfg_;
	std::atomic<bool> running_{false};
	std::atomic<bool> stop_{false};

	// Threads
	std::thread demux_thread_;
	std::thread ai_thread_;
	std::thread encode_thread_;

	// Ring buffers
	xop::RingBuffer<DecodedFrame>  decode_ring_;
	xop::RingBuffer<ProcessedFrame> process_ring_;
	xop::RingBuffer<AudioFrame>    audio_ring_;

	// CVs for blocking Pop — notified by producer when data is pushed,
	// waited on by consumer when ring is empty (replaces sleep-spin).
	std::mutex              decode_cv_mutex_;
	std::condition_variable decode_cv_;
	std::mutex              process_cv_mutex_;
	std::condition_variable process_cv_;

	// FFmpeg contexts (owned by demux thread, accessed only there)
	AVFormatContext*  ifmt_ctx_   = nullptr;
	AVCodecContext*   dec_ctx_    = nullptr;
	const AVCodec*    decoder_    = nullptr;
	int               video_idx_  = -1;
	SwsContext*       to_bgr_     = nullptr;   // decoder fmt → BGR24
	int               dec_width_  = 0;
	int               dec_height_ = 0;

	// Audio contexts (demux thread)
	int               audio_idx_    = -1;
	AVCodecContext*   audio_dec_ctx_ = nullptr;
	const AVCodec*    audio_decoder_ = nullptr;

	// Encoder contexts (owned by encode thread)
	AVCodecContext*   enc_ctx_    = nullptr;
	const AVCodec*    encoder_    = nullptr;
	SwsContext*       to_enc_     = nullptr;   // BGR24 → YUV420P
	AVFrame*          enc_in_     = nullptr;   // pre-allocated YUV420P
	int64_t           pts_base_us_ = 0;         // first frame capture time baseline

	// Stats
	std::atomic<int64_t> frames_decoded_{0};
	std::atomic<int64_t> dropped_demux_{0};
	std::atomic<int64_t> dropped_ai_{0};
	std::atomic<int64_t> pruned_demux_{0};
	std::atomic<int64_t> pruned_ai_{0};
	std::atomic<int64_t> audio_dropped_{0};
	std::atomic<int64_t> audio_pruned_{0};

	// Peak ring fill (stress testing)
	std::atomic<int> max_decode_ring_fill_{0};
	std::atomic<int> max_process_ring_fill_{0};
	std::atomic<int64_t> backpressure_events_{0};
};

} // namespace ffmpeg

#endif // FFMPEG_STREAM_PIPELINE_H
