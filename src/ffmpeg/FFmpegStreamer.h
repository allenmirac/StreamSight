// FFmpegStreamer.h
// Full FFmpeg C API pipeline: demux → decode → AI interception → encode → output.
// Replaces VideoSource + H264Encoder with a unified in-process pipeline.
// Supports optional audio passthrough via AudioFrameCallback.

#ifndef FFMPEG_STREAMER_H
#define FFMPEG_STREAMER_H

#include "FFmpegUtils.h"
#include "IOutputAdapter.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace ffmpeg {

// User-provided callback. Receives a decoded BGR24 frame and may
// modify pixels in-place (e.g., draw AI overlay).  Return false
// to drop the frame (skip encoding).
using FrameInterceptor = std::function<bool(FFmpegFrame& frame)>;

// Callback for decoded audio frames. Receives PCM data from the decoder.
// The callback should handle pushing audio into the RTSP/RTP pipeline.
using AudioFrameCallback = std::function<void(const AVFrame* decoded_audio)>;

struct StreamerConfig {
    // ── Input ───────────────────────────────────────────────
    std::string input_url;          // file path, v4l2:/dev/videoN, rtsp://...
    int         open_timeout_ms    = 5000;
    int         read_timeout_ms    = 3000;
    bool        reconnect_on_eof   = false;
    int         max_reconnect      = 10;
    int         reconnect_delay_ms = 2000;

    // ── Processing ──────────────────────────────────────────
    int              output_width  = 0;  // 0 = same as input
    int              output_height = 0;
    FrameInterceptor frame_cb;          // AI analysis + overlay
    AudioFrameCallback audio_cb;        // decoded audio output

    // ── Encoder ─────────────────────────────────────────────
    std::string codec_name = "libx264";
    std::string preset     = "ultrafast";
    std::string tune       = "zerolatency";
    int         bitrate    = 2000000;
    int         fps        = 25;
    int         gop_size   = 0;        // 0 = equal to fps
    int         threads    = 2;        // encoder thread count

    // ── Output ──────────────────────────────────────────────
    std::vector<std::shared_ptr<IOutputAdapter>> outputs;
};

class FFmpegStreamer {
public:
    explicit FFmpegStreamer(const StreamerConfig& cfg);
    ~FFmpegStreamer();

    // Lifecycle
    bool Open();
    void Close();
    bool IsOpened() const { return opened_; }

    // Process one frame: demux -> decode -> BGR convert ->
    //   AI callback -> YUV convert -> encode -> output.
    // Audio packets are decoded and dispatched via audio_cb inline.
    // Returns false on EOF or unrecoverable error.
    bool ProcessNextFrame();

    // Blocking loop until stop is set or EOF/error.
    void Run(std::atomic<bool>& stop_flag);

    // Accessors (valid after Open)
    int    GetWidth()  const { return dec_width_; }
    int    GetHeight() const { return dec_height_; }
    double GetFPS()    const { return cfg_.fps; }
    bool   HasAudio()  const { return audio_idx_ >= 0; }

private:
    // ── Internal stages ──────────────────────────────────
    bool OpenInput();
    bool OpenDecoder();
    bool OpenAudioDecoder();
    bool OpenEncoder();
    bool OpenScalers();
    bool OpenOutputs();

    // Core processing: read+decode one frame, return false on EOF/error.
    bool ReadAndDecode(AVFrame* decoded);

    // Decode one audio packet and dispatch via audio_cb.
    void ProcessAudioPacket(AVPacket* pkt);

    // Encode one frame and deliver to outputs.
    bool EncodeAndDeliver(AVFrame* enc_in);

    // Reconnect input on network error
    bool Reconnect();

    // ── FFmpeg contexts ──────────────────────────────────
    AVFormatContext*  ifmt_ctx_   = nullptr;
    AVCodecContext*   dec_ctx_    = nullptr;
    AVCodecContext*   enc_ctx_    = nullptr;
    AVCodecContext*   audio_dec_ctx_ = nullptr;
    const AVCodec*    decoder_    = nullptr;
    const AVCodec*    encoder_    = nullptr;
    int               video_idx_  = -1;
    int               audio_idx_  = -1;
    AVRational        src_tb_     = {1, 90000};

    // Scalers: decoder fmt → BGR24 (for AI), BGR24 → YUV420P (for encode)
    SwsContext*       to_bgr_     = nullptr;
    SwsContext*       to_enc_     = nullptr;

    // Pre-allocated frames (reused across ProcessNextFrame calls)
    AVFrame*          decoded_    = nullptr;  // video decoder output
    AVFrame*          bgr_        = nullptr;  // BGR24 for AI (persistent)
    AVFrame*          enc_in_     = nullptr;  // YUV420P encoder input (persistent)

    StreamerConfig    cfg_;
    bool              opened_     = false;
    int               dec_width_  = 0;
    int               dec_height_ = 0;
    AVPixelFormat     dec_pix_fmt_ = AV_PIX_FMT_NONE;
    int64_t           frame_seq_  = 0;
};

} // namespace ffmpeg

#endif // FFMPEG_STREAMER_H
