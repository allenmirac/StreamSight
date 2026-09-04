// StreamSession.cpp
// Unified session abstraction implementation.

#include "StreamSession.h"
#include "StreamServer.h"
#include "FFmpegStreamer.h"
#include "FFmpegUtils.h"
#include "RtspOutputAdapter.h"
#include "RtmpOutputAdapter.h"
#include "../xop/RtspServer.h"
#include "../xop/H264Source.h"
#include "../xop/AACSource.h"
#include "../xop/MediaSession.h"
#include "../net/EventLoop.h"
#include "../effect/EffectFactory.h"
#include "../effect/EffectChain.h"
#include "../effect/FaceRecognitionPlugin.h"
#include <iostream>
#include <chrono>

namespace ffmpeg {

StreamSession::StreamSession(const StreamSessionConfig& cfg)
    : cfg_(cfg)
{
}

StreamSession::~StreamSession() {
    Stop();
}

bool StreamSession::Start(StreamServer* server) {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (running_) return true;
    if (stopped_) return false;  // single-use, no restart

    if (!server || !server->GetRtspServer()) {
        std::cerr << "[StreamSession] no shared StreamServer available" << std::endl;
        return false;
    }

    rtsp_server_ = server->GetRtspServer();
    event_loop_  = server->GetEventLoop();

    xop::MediaSession* session = xop::MediaSession::CreateNew(cfg_.rtsp_suffix);
    session->AddSource(xop::channel_0, xop::H264Source::CreateNew(cfg_.fps));
    if (cfg_.enable_audio) {
        session->AddSource(xop::channel_1,
                           xop::AACSource::CreateNew(44100, 2, true));
    }
    session_id_ = rtsp_server_->AddSession(session);
    if (session_id_ == 0) {
        std::cerr << "[StreamSession] duplicate RTSP suffix: /" << cfg_.rtsp_suffix
                  << std::endl;
        delete session;  // AddSession did not take ownership on duplicate
        rtsp_server_ = nullptr;
        event_loop_  = nullptr;
        return false;
    }

    // ── Client-aware pipeline gating: register lifecycle callbacks ─────
    session->AddNotifyConnectedCallback(
        [this](xop::MediaSessionId, std::string, uint16_t) { OnClientConnected(); });
    session->AddNotifyDisconnectedCallback(
        [this](xop::MediaSessionId, std::string, uint16_t) { OnClientDisconnected(); });

    std::cout << "[StreamSession] RTSP: rtsp://localhost:" << rtsp_server_->GetPort()
              << "/" << cfg_.rtsp_suffix << std::endl;

    // ── EffectChain (from JSON or default FaceRecognition) ──────────────
    if (cfg_.enable_ai) {
        std::string json = cfg_.effects_json;
        if (json.empty()) {
            json = "{"
                   "\"detect_model\":\"models/face_detection.onnx\","
                   "\"recog_model\":\"models/face_recognition.onnx\","
                   "\"face_db_path\":\"faces.json\","
                   "\"event_log_path\":\"events.jsonl\","
                   "\"analyze_fps\":" + std::to_string(cfg_.analyze_fps) +
                   "}";
        }

        face_plugin_ = streamsight::EffectFactory::Create("FaceRecognition", json);
        if (face_plugin_ && face_plugin_->Open("")) {
            effect_chain_.AddPlugin(face_plugin_);
        } else if (face_plugin_) {
            std::cerr << "[StreamSession] FaceRecognitionPlugin Open failed, "
                      << "running without AI" << std::endl;
            face_plugin_.reset();
        }
    }

    // ── Output adapters ─────────────────────────────────────────────────
    rtsp_out_ = std::make_shared<RtspOutputAdapter>(
        rtsp_server_, session_id_, (int)xop::channel_0);

    if (!cfg_.rtmp_url.empty()) {
        rtmp_out_ = std::make_shared<RtmpOutputAdapter>(cfg_.rtmp_url);
    }

    // ── Run ─────────────────────────────────────────────────────────────
    start_time_.store(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    running_ = true;
    stop_ = false;

    if (cfg_.pipeline_mode == "parallel") {
        run_thread_ = std::thread(&StreamSession::RunParallel, this);
    } else {
        run_thread_ = std::thread(&StreamSession::RunSerial, this);
    }

    return true;
}

void StreamSession::Stop() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (running_) {
        stop_ = true;
        running_ = false;
        // Wake pipeline thread from WaitForClients() so it can check stop_ and exit
        client_cv_.notify_all();
    }

    // Join the run thread whether it is still running or finished naturally.
    if (run_thread_.joinable()) {
        run_thread_.join();
    }

    {
        std::shared_ptr<StreamPipeline> p;
        { std::lock_guard<std::mutex> lk(pipeline_mutex_); p = std::move(pipeline_); }
        if (p) p->Stop();
    }
    if (rtsp_server_ && session_id_ != 0) {
        rtsp_server_->RemoveSession(session_id_);
    }
    session_id_  = 0;
    rtsp_server_ = nullptr;
    event_loop_  = nullptr;
    if (face_plugin_) face_plugin_->Close();
    effect_chain_.Clear();
    stopped_ = true;
}

SessionStatus StreamSession::GetStatus() const {
    SessionStatus s;
    s.running = running_;
    s.frames_processed = frame_count_;
    if (cfg_.pipeline_mode == "parallel") {
        std::shared_ptr<StreamPipeline> p;
        { std::lock_guard<std::mutex> lk(pipeline_mutex_); p = pipeline_; }
        if (p) s.frames_processed = p->FramesDecoded();
    }
    int64_t st = start_time_.load();
    if (st > 0) {
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        s.uptime_seconds = now - st;
    }
    s.rtsp_port = rtsp_server_ ? rtsp_server_->GetPort() : 0;
    s.http_port = cfg_.http_port;
    s.stream_id = cfg_.rtsp_suffix;

    // Stress testing metrics (parallel mode only)
    if (cfg_.pipeline_mode == "parallel") {
        std::shared_ptr<StreamPipeline> p;
        { std::lock_guard<std::mutex> lk(pipeline_mutex_); p = pipeline_; }
        if (p) {
            s.frames_dropped   = p->FramesDroppedDemux() + p->FramesDroppedAI();
            s.frames_pruned    = p->FramesPrunedDemux() + p->FramesPrunedAI();
            s.decode_ring_fill   = p->DecodeRingFill();
            s.process_ring_fill  = p->ProcessRingFill();
            s.max_decode_ring_fill  = p->MaxDecodeRingFill();
            s.max_process_ring_fill = p->MaxProcessRingFill();
            s.backpressure_events   = p->BackpressureEvents();
        }
    }

    // Event loop performance
    if (event_loop_) {
        s.avg_eventloop_latency_us = event_loop_->GetAvgLoopUs();
        s.eventloop_active_fds     = event_loop_->GetActiveFdCount();
    }

    return s;
}

bool StreamSession::UpdateEffects(const std::string& effects_json) {
    effect_chain_.Clear();
    face_plugin_.reset();

    std::shared_ptr<streamsight::IEffectPlugin> plugin =
        streamsight::EffectFactory::Create("FaceRecognition", effects_json);
    if (plugin && plugin->Open("")) {
        face_plugin_ = plugin;
        effect_chain_.AddPlugin(face_plugin_);
        return true;
    }
    return false;
}

std::vector<std::string> StreamSession::GetEffectNames() const {
    std::vector<std::string> names;
    if (face_plugin_) names.push_back(face_plugin_->Name());
    return names;
}

void* StreamSession::GetRtspServer() const {
    return rtsp_server_;
}

uint32_t StreamSession::GetSessionId() const {
    return session_id_;
}

// ── Client-aware pipeline gating ─────────────────────────────────────────────

void StreamSession::OnClientConnected() {
    int prev = client_count_.fetch_add(1);
    if (prev == 0) {
        client_cv_.notify_all();  // wake pipeline from WaitForClients
    }
}

void StreamSession::OnClientDisconnected() {
    int prev = client_count_.fetch_sub(1);
    if (prev == 1) {
        // Last client left — pipeline will pause on next WaitForClients() call.
        // No notify needed; the predicate (client_count > 0) is now false.
    }
}

void StreamSession::WaitForClients() {
    std::unique_lock<std::mutex> lock(client_mutex_);
    client_cv_.wait(lock, [this] {
        return client_count_.load() > 0 || stop_.load();
    });
}

// ── Pipeline runners ────────────────────────────────────────────────────────

void StreamSession::RunSerial() {
    StreamerConfig scfg;
    scfg.input_url     = cfg_.input_url;
    scfg.fps           = cfg_.fps;
    scfg.bitrate       = cfg_.bitrate;
    scfg.threads       = cfg_.enc_threads;
    scfg.output_width  = cfg_.width;
    scfg.output_height = cfg_.height;

    if (!effect_chain_.Empty()) {
        scfg.frame_cb = [this](FFmpegFrame& f) -> bool {
            cv::Mat mat = FrameToMat(f);
            std::vector<streamsight::EffectResult> results;
            effect_chain_.ProcessFrame(
                mat.data, mat.cols, mat.rows, (int)mat.step, results);

            event_bus_.Publish(FrameProcessedEvent{
                cfg_.rtsp_suffix,
                static_cast<int64_t>(frame_count_++),
                static_cast<int64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count()),
                0,
                "[]"
            });

            return true;
        };
    }

    scfg.outputs.push_back(rtsp_out_);
    if (rtmp_out_) scfg.outputs.push_back(rtmp_out_);

    FFmpegStreamer streamer(scfg);
    if (!streamer.Open()) {
        std::cerr << "[StreamSession] FFmpegStreamer open failed" << std::endl;
        running_ = false;
        return;
    }

    while (!stop_) {
        WaitForClients();  // blocks until RTSP client connects
        if (stop_) break;

        if (!streamer.IsOpened()) break;

        if (!streamer.ProcessNextFrame()) {
            // EOF or transient error — attempt reconnect if configured.
            // RTSP sources may drop connections due to network jitter;
            // a single av_read_frame() failure should not kill the session.
            if (!cfg_.reconnect_on_eof) break;

            std::cerr << "[StreamSession] stream error, attempting reconnect..."
                      << std::endl;
            streamer.Close();

            bool reconnected = false;
            for (int i = 0; i < cfg_.max_reconnect && !stop_; ++i) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(cfg_.reconnect_delay_ms));
                WaitForClients();  // check stop_ flag
                if (stop_) break;
                if (streamer.Open()) {
                    reconnected = true;
                    std::cout << "[StreamSession] reconnected after "
                              << (i + 1) << " attempt(s)" << std::endl;
                    break;
                }
                std::cerr << "[StreamSession] reconnect attempt "
                          << (i + 1) << " failed" << std::endl;
            }

            if (!reconnected) {
                std::cerr << "[StreamSession] reconnect failed after "
                          << cfg_.max_reconnect << " attempts" << std::endl;
                break;
            }
            continue;
        }
    }

    streamer.Close();
    running_ = false;
}

void StreamSession::RunParallel() {
    PipelineConfig pcfg;
    pcfg.input_url     = cfg_.input_url;
    pcfg.fps           = cfg_.fps;
    pcfg.bitrate       = cfg_.bitrate;
    pcfg.threads       = cfg_.enc_threads;
    pcfg.output_width  = cfg_.width;
    pcfg.output_height = cfg_.height;
    pcfg.decode_ring_size  = cfg_.ringbuf_size;
    pcfg.process_ring_size = cfg_.ringbuf_size;
    pcfg.audio_ring_size   = cfg_.ringbuf_size * 2;
    pcfg.drop_policy.max_frame_age_us = static_cast<int64_t>(cfg_.max_frame_age_ms) * 1000LL;
    pcfg.drop_policy.time_window_us   = static_cast<int64_t>(cfg_.time_window_ms) * 1000LL;
    pcfg.enable_backpressure = true;
    pcfg.enable_client_gating = cfg_.enable_client_gating;
    pcfg.enable_audio = cfg_.enable_audio;

    // Client-aware gating: share pointers so DemuxDecodeLoop can wait for clients
    if (pcfg.enable_client_gating) {
        pcfg.has_clients = &client_count_;
        pcfg.client_cv   = &client_cv_;
        pcfg.client_mutex = &client_mutex_;
    }
    pcfg.audio_rtsp_server  = rtsp_server_;
    pcfg.audio_session_id   = session_id_;
    pcfg.audio_channel      = (int)xop::channel_1;

    if (!effect_chain_.Empty()) {
        pcfg.frame_cb = [this](FFmpegFrame& f) -> bool {
            cv::Mat mat = FrameToMat(f);
            std::vector<streamsight::EffectResult> results;
            return effect_chain_.ProcessFrame(
                mat.data, mat.cols, mat.rows, (int)mat.step, results);
        };
    }

    pcfg.outputs.push_back(rtsp_out_);
    if (rtmp_out_) pcfg.outputs.push_back(rtmp_out_);

    auto pipeline = std::make_shared<StreamPipeline>(cfg_.rtsp_suffix, pcfg);
    if (!pipeline->Start()) {
        std::cerr << "[StreamSession] pipeline start failed" << std::endl;
        running_ = false;
        return;
    }
    {
        std::lock_guard<std::mutex> lk(pipeline_mutex_);
        pipeline_ = std::move(pipeline);
    }

    while (!stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    {
        std::shared_ptr<StreamPipeline> p;
        { std::lock_guard<std::mutex> lk(pipeline_mutex_); p = std::move(pipeline_); }
        if (p) p->Stop();
    }
    running_ = false;
}

}  // namespace ffmpeg