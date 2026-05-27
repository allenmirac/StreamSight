// StreamSession.h
// Unified session abstraction that encapsulates EventLoop, RtspServer,
// MediaSession, FaceRecognitionPlugin, EffectChain, PipelineManager/
// FFmpegStreamer, RtspOutputAdapter, RtmpOutputAdapter, EventLogger,
// and HttpApiServer behind a clean Start/Stop/GetStatus interface.
//
// Supports two pipeline modes:
//   serial   — FFmpegStreamer (single-threaded demux+decode+AI+encode+output)
//   parallel — PipelineManager/StreamPipeline (3-stage with RingBuffers)

#ifndef FFMPEG_STREAM_SESSION_H
#define FFMPEG_STREAM_SESSION_H

#include "StreamPipeline.h"
#include "PipelineManager.h"
#include "IOutputAdapter.h"
#include "../effect/EffectChain.h"
#include "../observe/EventBus.h"
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>
#include <cstdint>

namespace xop {
class EventLoop;
class RtspServer;
using MediaSessionId = uint32_t;
}

namespace streamsight {
struct EffectResult;
class IEffectPlugin;
}

namespace ffmpeg {

// ── Configuration ──────────────────────────────────────────────────────────

struct StreamSessionConfig {
    // Input
    std::string input_url;
    int         width  = 640;
    int         height = 480;
    int         fps    = 25;

    // Network
    int         rtsp_port = 8554;
    std::string rtsp_suffix = "live";
    int         http_port = 8080;
    std::string rtmp_url;

    // Encoder
    int         bitrate    = 2000000;
    int         enc_threads = 2;

    // Pipeline mode
    std::string pipeline_mode = "serial";
    int         ringbuf_size   = 4;
    int         max_frame_age_ms = 500;
    int         time_window_ms   = 0;
    int         eventloop_threads = 2;

    // AI
    bool        enable_ai = true;
    int         analyze_fps = 5;

    // Audio
    bool        enable_audio = true;

    // Effects JSON (FaceRecognitionPlugin config)
    std::string effects_json;
};

// ── Status ─────────────────────────────────────────────────────────────────

struct SessionStatus {
    bool        running = false;
    int64_t     frames_processed = 0;
    int64_t     frames_dropped   = 0;
    int64_t     uptime_seconds   = 0;
    int         rtsp_port = 0;
    int         http_port = 0;
    std::string stream_id;
    std::string error;
};

// ── Events ─────────────────────────────────────────────────────────────────

struct FrameProcessedEvent {
    std::string stream_id;
    int64_t     frame_id;
    int64_t     timestamp_ms;
    int         face_count;
    std::string effect_results_json;
};

using SessionEventBus = streamsight::EventBus<FrameProcessedEvent>;

// ── StreamSession ──────────────────────────────────────────────────────────

class StreamSession {
public:
    explicit StreamSession(const StreamSessionConfig& cfg);
    ~StreamSession();

    // Non-copyable
    StreamSession(const StreamSession&) = delete;
    StreamSession& operator=(const StreamSession&) = delete;

    // Start the full pipeline. Returns false on port bind failure or
    // configuration error. Idempotent if already running.
    bool Start();

    // Stop all processing, close AI plugins, and join threads.
    // Idempotent if already stopped.
    void Stop();

    bool IsRunning() const { return running_; }

    SessionStatus GetStatus() const;

    // Replace the EffectChain with a new FaceRecognitionPlugin configured
    // from JSON. Returns false if the plugin could not be created/opened.
    bool UpdateEffects(const std::string& effects_json);

    // Names of active effect plugins.
    std::vector<std::string> GetEffectNames() const;

    // Event bus for frame-processing notifications.
    SessionEventBus& GetEventBus() { return event_bus_; }
    const StreamSessionConfig& Config() const { return cfg_; }

    // Raw pointer to RtspServer (for external integration, e.g. HttpApiServer).
    void* GetRtspServer() const;

    // MediaSessionId assigned by the RTSP server.
    uint32_t GetSessionId() const;

    // FaceRecognitionPlugin (may be nullptr if AI is disabled or init failed).
    std::shared_ptr<streamsight::IEffectPlugin> GetFacePlugin() const {
        return face_plugin_;
    }

private:
    void RunSerial();
    void RunParallel();

    StreamSessionConfig  cfg_;
    std::atomic<bool>    running_{false};
    std::atomic<bool>    stop_{false};
    std::atomic<bool>    stopped_{false};  // single-use: no restart after Stop
    mutable std::mutex   lifecycle_mutex_;

    // RTSP infrastructure. EventLoop is explicitly started; RtspServer
    // shared_ptr keeps the server alive for the session lifetime.
    std::shared_ptr<xop::EventLoop>   event_loop_;
    std::shared_ptr<xop::RtspServer>  rtsp_server_;
    xop::MediaSessionId               session_id_  = 0;

    // AI / Effect plugin chain
    streamsight::EffectChain           effect_chain_;
    std::shared_ptr<streamsight::IEffectPlugin> face_plugin_;

    // Pipeline (used in parallel mode)
    PipelineManager                    pipeline_mgr_;

    // Output adapters
    std::shared_ptr<IOutputAdapter>    rtsp_out_;
    std::shared_ptr<IOutputAdapter>    rtmp_out_;

    // Background run thread
    std::thread                        run_thread_;

    // ── Client-aware pipeline gating ────────────────────────────────────
    // Pipeline runs only when RTSP clients are connected (event-driven).
    // client_count_ tracks total connected clients (atomic for lock-free
    // read from NotifyConnectedCallback / NotifyDisconnectedCallback).
    std::atomic<int>                   client_count_{0};
    std::mutex                         client_mutex_;
    std::condition_variable            client_cv_;

    void OnClientConnected();
    void OnClientDisconnected();
    void WaitForClients();

    // Event bus for external observers
    SessionEventBus                    event_bus_;

    // Stats
    std::atomic<int64_t>               frame_count_{0};
    std::atomic<int64_t>               start_time_{0};
};

}  // namespace ffmpeg

#endif  // FFMPEG_STREAM_SESSION_H
