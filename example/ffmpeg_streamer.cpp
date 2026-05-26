// ffmpeg_streamer.cpp
// Standalone demo: FFmpeg C API full pipeline with AI analysis and RTSP push.
//
// Build: make ffmpeg_streamer
// Run:   ./bin/ffmpeg_streamer --input test.h264 --port 8554
//        ./bin/ffmpeg_streamer --input test.h264 --rtmp rtmp://localhost/live/test --port 8554
//        ./bin/ffmpeg_streamer --input /dev/video0 --source camera --port 8554

// StreamSight Platform (Phase 2)
#include "ffmpeg/StreamSession.h"
#include "api/StreamApiServer.h"
#include "effect/FaceRecognitionPlugin.h"

#include "xop/RtspServer.h"
#include "xop/H264Source.h"
#include "xop/AACSource.h"
#include "net/EventLoop.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <map>
#include <sstream>
#include <csignal>
#include <cstdlib>

extern "C" {
#include <libavutil/samplefmt.h>
}

static std::atomic<bool> g_stop{false};
static void OnSignal(int) { g_stop = true; }

static std::map<std::string, std::string> ParseArgs(int argc, char** argv) {
    std::map<std::string, std::string> args;
    args["source"]       = "file";
    args["input"]        = "test.h264";
    args["width"]        = "640";
    args["height"]       = "480";
    args["fps"]          = "25";
    args["port"]         = "8554";
    args["http-port"]    = "8080";
    args["bitrate"]      = "2000000";
    args["suffix"]       = "live";
    args["detect-model"] = "./models/face_detection.onnx";
    args["recog-model"]  = "./models/face_recognition.onnx";
    args["db"]           = "faces.json";
    args["log"]          = "events.jsonl";
    args["analyze-fps"]  = "5";
    args["no-ai"]        = "0";
    args["rtmp"]         = "";
    args["threads"]      = "2";
    args["pipeline-mode"]  = "serial";
    args["ringbuf-size"]   = "4";
    args["max-frame-age-ms"] = "500";
    args["time-window-ms"] = "0";
    args["eventloop-threads"] = "2";
    args["enable-audio"]   = "1";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-ai") { args["no-ai"] = "1"; continue; }
        if (a == "--no-audio") { args["enable-audio"] = "0"; continue; }
        if (a.substr(0, 2) == "--" && i + 1 < argc) {
            args[a.substr(2)] = argv[++i];
        }
    }
    return args;
}

int main(int argc, char** argv) {
    signal(SIGINT,  OnSignal);
    signal(SIGTERM, OnSignal);

    auto args = ParseArgs(argc, argv);

    std::cout << "[Main] StreamSight Phase 2 — StreamSession + StreamApiServer"
              << std::endl;

    // ── Build StreamSession config from CLI args ──────────────
    ffmpeg::StreamSessionConfig cfg;
    cfg.input_url       = (args["source"] == "camera")
                          ? ("/dev/video" + args["input"]) : args["input"];
    cfg.width           = std::stoi(args["width"]);
    cfg.height          = std::stoi(args["height"]);
    cfg.fps             = std::stoi(args["fps"]);
    cfg.rtsp_port       = std::stoi(args["port"]);
    cfg.http_port       = std::stoi(args["http-port"]);
    cfg.bitrate         = std::stoi(args["bitrate"]);
    cfg.enc_threads     = std::stoi(args["threads"]);
    cfg.enable_ai       = (args["no-ai"] != "1");
    cfg.analyze_fps     = std::stoi(args["analyze-fps"]);
    cfg.rtmp_url        = args["rtmp"];
    cfg.rtsp_suffix     = args["suffix"];
    cfg.pipeline_mode   = args["pipeline-mode"];
    cfg.ringbuf_size    = std::stoi(args["ringbuf-size"]);
    cfg.max_frame_age_ms = std::stoi(args["max-frame-age-ms"]);
    cfg.time_window_ms  = std::stoi(args["time-window-ms"]);
    cfg.enable_audio    = (args["enable-audio"] != "0");

    // Pass AI model paths via effects_json
    if (cfg.enable_ai) {
        std::ostringstream json;
        json << "{"
             << "\"detect_model\":\"" << args["detect-model"] << "\","
             << "\"recog_model\":\"" << args["recog-model"] << "\","
             << "\"face_db_path\":\"" << args["db"] << "\","
             << "\"event_log_path\":\"" << args["log"] << "\","
             << "\"analyze_fps\":" << cfg.analyze_fps
             << "}";
        cfg.effects_json = json.str();
    }

    // ── Create and start session ─────────────────────────────
    auto session = std::make_shared<ffmpeg::StreamSession>(cfg);
    if (!session->Start()) {
        std::cerr << "[Main] StreamSession start failed" << std::endl;
        return 1;
    }

    std::cout << "[Main] RTSP: rtsp://localhost:" << cfg.rtsp_port
              << "/" << cfg.rtsp_suffix << std::endl;
    if (!cfg.rtmp_url.empty()) {
        std::cout << "[Main] RTMP: " << cfg.rtmp_url << std::endl;
    }

    // ── HTTP API server (merged — replaces ai::HttpApiServer) ──
    ai::FaceDatabase*   face_db = nullptr;
    ai::FaceRecognizer* face_recog = nullptr;

    // Get face components from the session's plugin for legacy API routes
    auto face_plugin = std::dynamic_pointer_cast<streamsight::FaceRecognitionPlugin>(
        session->GetFacePlugin());
    if (face_plugin) {
        face_db   = face_plugin->GetDatabase();
        face_recog = face_plugin->GetRecognizer();
    }

    api::StreamApiServer api_server(cfg.http_port, face_db, face_recog);
    std::string session_id = api_server.RegisterSession(session);
    api_server.Start();

    // Subscribe to session EventBus to feed legacy /api/current and /api/events
    auto bus_handle = session->GetEventBus().Subscribe(
        [&api_server](const ffmpeg::FrameProcessedEvent& e) {
            ai::AnalysisResult r;
            r.frame_id = static_cast<int>(e.frame_id);
            r.timestamp_ms = e.timestamp_ms;
            api_server.UpdateResult(r);
            api_server.AddEvent(r);
        });

    std::cout << "[Main] HTTP API: http://localhost:" << cfg.http_port << std::endl;
    std::cout << "[Main] Session ID: " << session_id << std::endl;
    std::cout << "[Main] Running. Ctrl+C to stop." << std::endl;

    // ── Wait for stop signal ─────────────────────────────────
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // ── Cleanup ──────────────────────────────────────────────
    session->GetEventBus().Unsubscribe(bus_handle);
    api_server.Stop();
    session->Stop();

    std::cout << "[Main] Done." << std::endl;
    return 0;
}