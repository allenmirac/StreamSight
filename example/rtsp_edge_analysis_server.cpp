#include "control/StreamManager.h"
#include "control/Scheduler.h"
#include "control/StreamTask.h"
#include "control/PolicyCenter.h"
#include "observe/MetricsRegistry.h"
#include "cdn_sim/EdgeNodePool.h"

#include "net/Timer.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>

static std::atomic<bool> g_stop{false};
static void OnSignal(int) { g_stop = true; }

static std::map<std::string, std::string> ParseArgs(int argc, char** argv) {
    std::map<std::string, std::string> args;
    args["stream-id"]     = "live_001";
    args["source"]        = "file";
    args["device"]        = "0";
    args["input"]         = "test.h264";
    args["width"]         = "640";
    args["height"]        = "480";
    args["fps"]           = "25";
    args["port"]          = "8554";
    args["suffix"]        = "live_001";
    args["region"]        = "local";
    args["bitrate"]       = "2048";
    args["detect-model"]  = "models/face_detection.onnx";
    args["recog-model"]   = "models/face_recognition.onnx";
    args["db"]            = "faces.json";
    args["log"]           = "events.jsonl";
    args["analyze-fps"]   = "5";
    args["no-ai"]         = "0";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-ai") {
            args["no-ai"] = "1";
            continue;
        }
        if (a.rfind("--", 0) == 0 && i + 1 < argc) {
            args[a.substr(2)] = argv[++i];
        }
    }
    return args;
}

int main(int argc, char** argv) {
    ::signal(SIGINT,  OnSignal);
    ::signal(SIGTERM, OnSignal);

    auto args = ParseArgs(argc, argv);

    control::StreamTask task;
    task.stream_id = args["stream-id"];
    task.session_suffix = args["suffix"];
    task.source_type = control::ParseSourceType(args["source"]);
    task.camera_device = std::stoi(args["device"]);
    task.source_uri = args["input"];
    task.width = std::stoi(args["width"]);
    task.height = std::stoi(args["height"]);
    task.fps = std::stoi(args["fps"]);
    task.rtsp_port = std::stoi(args["port"]);
    task.region = control::ParseRegion(args["region"]);
    task.target_bitrate_kbps = std::stoi(args["bitrate"]);
    task.detect_model = args["detect-model"];
    task.recog_model = args["recog-model"];
    task.face_db_path = args["db"];
    task.log_path = args["log"];
    task.analyze_fps = std::stoi(args["analyze-fps"]);
    task.enable_ai = (args["no-ai"] != "1");
    task.enable_overlay = true;
    task.loop_input = true;
    task.max_failover = 2;

    auto& metrics = observe::MetricsRegistry::Instance();

    auto pool = std::make_shared<cdn_sim::EdgeNodePool>();
    pool->AddNode(std::make_shared<cdn_sim::EdgeNode>(
        cdn_sim::EdgeNodeSpec{
            "edge_east_high", control::Region::East,
            cdn_sim::EdgeNodeType::HighCapacity,
            16, 8, 32
        },
        &metrics));

    pool->AddNode(std::make_shared<cdn_sim::EdgeNode>(
        cdn_sim::EdgeNodeSpec{
            "edge_east_medium", control::Region::East,
            cdn_sim::EdgeNodeType::MediumCapacity,
            8, 4, 16
        },
        &metrics));

    pool->AddNode(std::make_shared<cdn_sim::EdgeNode>(
        cdn_sim::EdgeNodeSpec{
            "edge_west_low", control::Region::West,
            cdn_sim::EdgeNodeType::LowCapacity,
            4, 2, 8
        },
        &metrics));

    control::SchedulerPolicy policy;
    policy.region_weight = 0.35;
    policy.load_weight = 0.30;
    policy.capability_weight = 0.25;
    policy.latency_weight = 0.08;
    policy.failover_weight = 0.02;
    policy.max_queue_threshold = 24;
    policy.busy_util_threshold = 0.90;

    auto scheduler = std::make_shared<control::Scheduler>(pool, policy);

    control::StreamManager manager(
        scheduler,
        pool,
        &metrics,
        "0.0.0.0",
        task.rtsp_port);

    if (!manager.Start()) {
        return 1;
    }

    if (!manager.StartStream(task)) {
        return 1;
    }

    std::cout << "[Main] Edge-aware pipeline running. Ctrl+C to stop." << std::endl;

    while (!g_stop) {
        xop::Timer::Sleep(2000);

        // auto snap = metrics.Snapshot();
        // std::cout << "---- metrics snapshot ----" << std::endl;
        // for (const auto& kv : snap) {
        //     std::cout << kv.first << " = " << kv.second << std::endl;
        // }
    }

    manager.StopAll();
    xop::Timer::Sleep(1000);
    std::cout << "[Main] stopped." << std::endl;
    return 0;
}
