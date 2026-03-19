// rtsp_analysis_server.cpp
// Complete video behavior analysis pipeline:
//   VideoSource → AI Analysis → Frame Overlay → H264 Encode → RTSP Push
//   + REST API (HTTP) for querying results and managing face database
//
// Usage:
//   ./rtsp_analysis_server [options]
//
//   --source   camera|file|rtsp       (default: file)
//   --device   <int>                  camera device index (default: 0)
//   --input    <path or URL>          file path / RTSP URL (default: test.h264)
//   --width    <int>                  encode width  (default: 640)
//   --height   <int>                  encode height (default: 480)
//   --fps      <int>                  encode / capture FPS (default: 25)
//   --port     <int>                  RTSP port (default: 554)
//   --http-port <int>                 HTTP API port (default: 8080)
//   --suffix   <string>               RTSP stream suffix (default: live)
//   --detect-model  <path>            Face detection ONNX (default: models/face_detection.onnx)
//   --recog-model   <path>            Face recognition ONNX (default: models/face_recognition.onnx)
//   --db        <path>                Face database JSON (default: faces.json)
//   --log       <path>                Event log file (default: events.jsonl)
//   --analyze-fps <int>               AI analysis rate (default: 5)
//   --no-ai                           Disable AI (just encode and stream)

#include "xop/RtspServer.h"
#include "xop/H264Source.h"
#include "net/Timer.h"

#include "ai/VideoSource.h"
#include "ai/CameraSource.h"
#include "ai/FileSource.h"
#include "ai/RtspPullSource.h"
#include "ai/H264Encoder.h"
#include "ai/FaceDetector.h"
#include "ai/FaceRecognizer.h"
#include "ai/FaceDatabase.h"
#include "ai/FrameAnalyzer.h"
#include "ai/FrameOverlay.h"
#include "ai/HttpApiServer.h"
#include "ai/EventLogger.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <map>
#include <cstdlib>
#include <csignal>

// ─── Global stop flag ─────────────────────────────────────────────────────────
static std::atomic<bool> g_stop{false};
static void OnSignal(int) { g_stop = true; }

// ─── CLI argument parser ──────────────────────────────────────────────────────
static std::map<std::string, std::string> ParseArgs(int argc, char** argv) {
    std::map<std::string, std::string> args;
    // Defaults
    args["source"]       = "file";
    args["device"]       = "0";
    args["input"]        = "test.h264";
    args["width"]        = "640";
    args["height"]       = "480";
    args["fps"]          = "25";
    args["port"]         = "554";
    args["http-port"]    = "8080";
    args["suffix"]       = "live";
    args["detect-model"] = "models/face_detection.onnx";
    args["recog-model"]  = "models/face_recognition.onnx";
    args["db"]           = "faces.json";
    args["log"]          = "events.jsonl";
    args["analyze-fps"]  = "5";
    args["no-ai"]        = "0";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-ai") { args["no-ai"] = "1"; continue; }
        if (a.substr(0, 2) == "--" && i + 1 < argc) {
            args[a.substr(2)] = argv[++i];
        }
    }
    return args;
}

// ─── Pipeline thread ──────────────────────────────────────────────────────────
struct Pipeline {
    ai::VideoSource*    source     = nullptr;
    ai::H264Encoder*    encoder    = nullptr;
    ai::FrameAnalyzer*  analyzer   = nullptr;
    ai::FrameOverlay*   overlay    = nullptr;
    ai::HttpApiServer*  api_server = nullptr;
    ai::EventLogger*    logger     = nullptr;
    xop::RtspServer*    rtsp       = nullptr;
    xop::MediaSessionId session_id = 0;
    int                 width      = 640;
    int                 height     = 480;
    bool                no_ai      = false;
};

static void RunPipeline(Pipeline& p) {
    cv::Mat frame;
    while (!g_stop) {
        if (!p.source->GrabFrame(frame)) {
            std::cerr << "[Pipeline] Source EOF / error." << std::endl;
            break;
        }

        // Resize to target dimensions
        if (frame.cols != p.width || frame.rows != p.height) {
            cv::resize(frame, frame, cv::Size(p.width, p.height));
        }

        // AI analysis + overlay
        if (!p.no_ai && p.analyzer) {
            ai::AnalysisResult result = p.analyzer->Analyze(frame);

            if (p.overlay) p.overlay->Draw(frame, result);

            if (p.api_server) {
                p.api_server->UpdateResult(result);
                p.api_server->AddEvent(result);
            }
        }

        // Encode frame
        if (p.encoder) p.encoder->EncodeFrame(frame);
    }
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    ::signal(SIGINT,  OnSignal);
    ::signal(SIGTERM, OnSignal);

    auto args    = ParseArgs(argc, argv);
    int  width   = std::stoi(args["width"]);
    int  height  = std::stoi(args["height"]);
    int  fps     = std::stoi(args["fps"]);
    int  rtsp_port = std::stoi(args["port"]);
    int  http_port = std::stoi(args["http-port"]);
    bool no_ai   = (args["no-ai"] == "1");
    int  afps    = std::stoi(args["analyze-fps"]);

    // ── Video source ─────────────────────────────────────────────────────────
    std::unique_ptr<ai::VideoSource> source;
    if (args["source"] == "camera") {
        int dev = std::stoi(args["device"]);
        source.reset(new ai::CameraSource(dev, width, height, fps));
    } else if (args["source"] == "rtsp") {
        source.reset(new ai::RtspPullSource(args["input"]));
    } else {
        source.reset(new ai::FileSource(args["input"], /*loop=*/true));
    }

    if (!source->Open()) {
        std::cerr << "[Main] Failed to open video source." << std::endl;
        return 1;
    }
    std::cout << "[Main] Source opened: " << args["source"]
              << "  " << source->GetWidth() << "x" << source->GetHeight()
              << " @ " << source->GetFPS() << " fps" << std::endl;

    // ── AI modules ───────────────────────────────────────────────────────────
    std::unique_ptr<ai::FaceDetector>   detector;
    std::unique_ptr<ai::FaceRecognizer> recognizer;
    std::unique_ptr<ai::FaceDatabase>   database;
    std::unique_ptr<ai::FrameAnalyzer>  analyzer;
    std::unique_ptr<ai::FrameOverlay>   overlay;
    std::unique_ptr<ai::EventLogger>    logger;

    if (!no_ai) {
        detector.reset(new ai::FaceDetector(args["detect-model"]));
        if (!detector->Load()) {
            std::cerr << "[Main] Warning: face detection model not loaded. "
                         "Run with --no-ai or place model at: "
                      << args["detect-model"] << std::endl;
            detector.reset();
        }

        recognizer.reset(new ai::FaceRecognizer(args["recog-model"]));
        if (!recognizer->Load()) {
            std::cerr << "[Main] Warning: face recognition model not loaded." << std::endl;
            recognizer.reset();
        }

        database.reset(new ai::FaceDatabase(args["db"]));
        int n = database->Load();
        std::cout << "[Main] Face database: " << n << " entries loaded." << std::endl;

        analyzer.reset(new ai::FrameAnalyzer(
            detector.get(), recognizer.get(), database.get()));
        analyzer->SetAnalyzeRate(afps);

        overlay.reset(new ai::FrameOverlay());

        logger.reset(new ai::EventLogger(args["log"]));
        logger->Open();
        analyzer->SetEventCallback([&](const ai::AnalysisResult& r) {
            logger->Log(r);
        });
    }

    // ── RTSP server ──────────────────────────────────────────────────────────
    std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());
    std::shared_ptr<xop::RtspServer> rtsp_server =
        xop::RtspServer::Create(event_loop.get());

    if (!rtsp_server->Start("0.0.0.0", rtsp_port)) {
        std::cerr << "[Main] RTSP server failed to bind on port "
                  << rtsp_port << std::endl;
        return 1;
    }

    xop::MediaSession* session = xop::MediaSession::CreateNew(args["suffix"]);
    session->AddSource(xop::channel_0, xop::H264Source::CreateNew(fps));
    session->AddNotifyConnectedCallback(
        [](xop::MediaSessionId, std::string ip, uint16_t port) {
            std::cout << "[RTSP] Client connected: " << ip << ":" << port << std::endl;
        });
    session->AddNotifyDisconnectedCallback(
        [](xop::MediaSessionId, std::string ip, uint16_t port) {
            std::cout << "[RTSP] Client disconnected: " << ip << ":" << port << std::endl;
        });
    xop::MediaSessionId session_id = rtsp_server->AddSession(session);

    std::cout << "[Main] RTSP URL: rtsp://localhost:" << rtsp_port
              << "/" << args["suffix"] << std::endl;

    // ── H264 encoder ─────────────────────────────────────────────────────────
    ai::H264Encoder encoder(width, height, fps);
    encoder.SetOutputCallback([&](const uint8_t* data, size_t len, bool key) {
        xop::AVFrame frame(len);
        frame.type      = key ? xop::VIDEO_FRAME_I : xop::VIDEO_FRAME_P;
        frame.timestamp = xop::H264Source::GetTimestamp();
        memcpy(frame.buffer.get(), data, len);
        rtsp_server->PushFrame(session_id, xop::channel_0, frame);
    });

    if (!encoder.Open()) {
        std::cerr << "[Main] H264Encoder failed to open (is ffmpeg installed?)."
                  << std::endl;
        return 1;
    }

    // ── HTTP API server ───────────────────────────────────────────────────────
    std::unique_ptr<ai::HttpApiServer> api_server;
    if (!no_ai) {
        api_server.reset(new ai::HttpApiServer(
            http_port, database.get(), recognizer.get()));
        api_server->Start();
        std::cout << "[Main] HTTP API: http://localhost:" << http_port << std::endl;
    }

    // ── Run pipeline ──────────────────────────────────────────────────────────
    Pipeline p;
    p.source     = source.get();
    p.encoder    = &encoder;
    p.analyzer   = analyzer.get();
    p.overlay    = overlay.get();
    p.api_server = api_server.get();
    p.logger     = logger.get();
    p.rtsp       = rtsp_server.get();
    p.session_id = session_id;
    p.width      = width;
    p.height     = height;
    p.no_ai      = no_ai;

    std::thread pipeline_thread(RunPipeline, std::ref(p));

    std::cout << "[Main] Pipeline running. Press Ctrl+C to stop." << std::endl;
    while (!g_stop) {
        xop::Timer::Sleep(200);
    }

    std::cout << "[Main] Stopping..." << std::endl;
    g_stop = true;

    source->Close();
    if (pipeline_thread.joinable()) pipeline_thread.join();
    encoder.Close();
    if (api_server) api_server->Stop();
    rtsp_server->RemoveSession(session_id);

    std::cout << "[Main] Done." << std::endl;
    return 0;
}
