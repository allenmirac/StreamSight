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
#include <condition_variable>
#include <mutex>
#include <chrono>

// ─── 改进的信号处理 ─────────────────────────────────────────────────────────
static std::atomic<bool> g_stop{false};
static std::condition_variable g_cv;
static std::mutex g_cv_mutex;
static int g_signal_count = 0;

static void OnSignal(int signal) {
    g_signal_count++;
    
    if (g_signal_count >= 3) {
        std::cerr << "\n[Signal] Received " << g_signal_count 
                  << " signals, forcing immediate exit!" << std::endl;
        _exit(1);
    }
    
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[Signal] Received " << (signal == SIGINT ? "SIGINT" : "SIGTERM") 
                  << ", initiating graceful shutdown..." << std::endl;
        g_stop = true;
        g_cv.notify_all();  // 唤醒所有等待的线程
    }
}

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
    args["detect-model"] = "./models/face_detection.onnx";
    args["recog-model"]  = "./models/face_recognition.onnx";
    args["db"]           = "faces.json";
    args["log"]          = "events.jsonl";
    args["analyze-fps"]  = "5";
    args["no-ai"]        = "0";
    args["timeout"]      = "100";  // 添加超时参数

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-ai") { args["no-ai"] = "1"; continue; }
        if (a.substr(0, 2) == "--" && i + 1 < argc) {
            args[a.substr(2)] = argv[++i];
        }
    }
    return args;
}

// ─── Pipeline 结构 ──────────────────────────────────────────────────────────
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
    int                 timeout_ms = 100;
};

// ─── 改进的 Pipeline 线程 ────────────────────────────────────────────────────
static void RunPipeline(Pipeline& p) {
    cv::Mat frame;
    int consecutive_failures = 0;
    const int MAX_CONSECUTIVE_FAILURES = 30;
    
    auto last_frame_time = std::chrono::steady_clock::now();
    auto frame_interval = std::chrono::milliseconds(1000 / 30); // 默认30fps
    
    if (p.encoder) {
        frame_interval = std::chrono::milliseconds(1000 / int(p.encoder->GetFPS()));
    }
    
    std::cout << "[Pipeline] Thread started, frame interval: " 
              << frame_interval.count() << "ms" << std::endl;
    
    while (!g_stop) {
        // 帧率控制
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time);
        
        if (elapsed < frame_interval) {
            std::this_thread::sleep_for(frame_interval - elapsed);
            continue;
        }
        
        // 获取帧
        bool got_frame = false;
        try {
            got_frame = p.source->GrabFrame(frame);
        } catch (const std::exception& e) {
            std::cerr << "[Pipeline] Exception in GrabFrame: " << e.what() << std::endl;
            consecutive_failures++;
            std::this_thread::sleep_for(std::chrono::milliseconds(p.timeout_ms));
            continue;
        }
        
        if (!got_frame) {
            consecutive_failures++;
            if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
                std::cerr << "[Pipeline] Too many consecutive failures (" 
                          << consecutive_failures << "), stopping pipeline." << std::endl;
                g_stop = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(p.timeout_ms));
            continue;
        }
        
        // 成功获取帧，重置失败计数器
        if (consecutive_failures > 0) {
            std::cout << "[Pipeline] Resumed normal operation after " 
                      << consecutive_failures << " failures" << std::endl;
            consecutive_failures = 0;
        }
        
        last_frame_time = now;
        
        // 调整大小
        if (frame.cols != p.width || frame.rows != p.height) {
            cv::resize(frame, frame, cv::Size(p.width, p.height));
        }

        // 关键修复：统一转换为编码器所需格式
        cv::Mat processed_frame = frame;
        if (p.encoder->requiresYUV() && frame.type() == CV_8UC3) {
            cv::cvtColor(frame, processed_frame, cv::COLOR_BGR2YUV_I420);
            // std::cout << "[FIX] Converted BGR to YUV_I420 for NO-AI path" << std::endl;
        }

        // AI 分析和叠加
        if (!p.no_ai && p.analyzer) {
            try {
                ai::AnalysisResult result = p.analyzer->Analyze(frame);
                
                if (p.overlay) {
                    p.overlay->Draw(frame, result);
                }
                
                if (p.api_server) {
                    p.api_server->UpdateResult(result);
                    p.api_server->AddEvent(result);
                }
            } catch (const std::exception& e) {
                std::cerr << "[Pipeline] AI Analysis error: " << e.what() << std::endl;
            }
        }

        // 编码
        if (p.encoder) {
            try {
                p.encoder->EncodeFrame(frame);
            } catch (const std::exception& e) {
                std::cerr << "[Pipeline] Encoding error: " << e.what() << std::endl;
            }
        }
    }
    
    std::cout << "[Pipeline] Thread exiting" << std::endl;
}

// ─── RTSP 服务器线程 ─────────────────────────────────────────────────────────
static void RunRtspServer(std::shared_ptr<xop::RtspServer> rtsp_server, 
                          std::shared_ptr<xop::EventLoop> event_loop) {
    std::cout << "[RTSP] Server thread started" << std::endl;
    
    while (!g_stop) {
        event_loop->Loop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "[RTSP] Server thread exiting" << std::endl;
}

// ─── 主函数 ─────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    // 设置信号处理器
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = OnSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // 自动重启被信号中断的系统调用
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);

    // Limit concurrency to avoid core contention between FFmpeg and OpenCV threads.
    cv::setNumThreads(2);
    ::setenv("OMP_NUM_THREADS", "2", 1);
    ::setenv("OPENCV_THREADS", "2", 1);

    std::cout << "[Main] Starting rtsp_analysis_server (fixed version)" << std::endl;
    
    auto args    = ParseArgs(argc, argv);
    int  width   = std::stoi(args["width"]);
    int  height  = std::stoi(args["height"]);
    int  fps     = std::stoi(args["fps"]);
    int  rtsp_port = std::stoi(args["port"]);
    int  http_port = std::stoi(args["http-port"]);
    bool no_ai   = (args["no-ai"] == "1");
    int  afps    = std::stoi(args["analyze-fps"]);
    int  timeout_ms = std::stoi(args["timeout"]);
    
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
        std::cout << "detect-model: " << args["detect-model"] << std::endl;
        if (!detector->Load()) {
            std::cerr << "[Main] Warning: face detection model not loaded." << std::endl;
            detector.reset();
        }

        recognizer.reset(new ai::FaceRecognizer(args["recog-model"]));
        std::cout << "recog-model: " << args["recog-model"] << std::endl;
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
            if (logger) logger->Log(r);
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
        if (rtsp_server && session_id > 0) {
            xop::AVFrame frame(len);
            frame.type      = key ? xop::VIDEO_FRAME_I : xop::VIDEO_FRAME_P;
            frame.timestamp = xop::H264Source::GetTimestamp();
            memcpy(frame.buffer.get(), data, len);
            rtsp_server->PushFrame(session_id, xop::channel_0, frame);
        }
    });

    if (!encoder.Open()) {
        std::cerr << "[Main] H264Encoder failed to open." << std::endl;
        return 1;
    }

    // ── HTTP API server ───────────────────────────────────────────────────────
    std::unique_ptr<ai::HttpApiServer> api_server;
    if (!no_ai) {
        api_server.reset(new ai::HttpApiServer(
            http_port, database.get(), recognizer.get()));
        if (!api_server->Start()) {
            std::cerr << "[Main] Failed to start HTTP API server" << std::endl;
        } else {
            std::cout << "[Main] HTTP API: http://localhost:" << http_port << std::endl;
        }
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
    p.timeout_ms = timeout_ms;

    std::thread pipeline_thread(RunPipeline, std::ref(p));
    std::thread rtsp_thread(RunRtspServer, rtsp_server, event_loop);

    std::cout << "[Main] Pipeline running. Press Ctrl+C to stop." << std::endl;
    
    // 主循环等待退出信号
    {
        std::unique_lock<std::mutex> lock(g_cv_mutex);
        g_cv.wait(lock, []{ return g_stop.load(); });
    }
    
    std::cout << "[Main] Stopping..." << std::endl;
    
    // 等待线程结束（带超时）
    auto wait_with_timeout = [](std::thread& t, int seconds) {
        if (t.joinable()) {
            auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
                if (t.joinable()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } else {
                    break;
                }
            }
            if (t.joinable()) {
                t.detach();
                std::cerr << "[Main] Thread did not finish in time, detached" << std::endl;
            }
        }
    };
    
    wait_with_timeout(pipeline_thread, 3);
    wait_with_timeout(rtsp_thread, 2);
    
    // 清理资源
    source->Close();
    encoder.Close();
    if (api_server) api_server->Stop();
    if (rtsp_server) rtsp_server->RemoveSession(session_id);
    if (logger) logger->Close();
    if (database) database->Save();
    
    std::cout << "[Main] Done." << std::endl;
    return 0;
}