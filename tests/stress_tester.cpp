// tests/stress_tester.cpp
// Multi-stream stress tester for StreamSight.
// Creates N StreamSessions, collects metrics, outputs JSON report.

#include "ffmpeg/StreamSession.h"
#include "ffmpeg/StreamServer.h"
#include "observe/LatencyTracer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <numeric>

// ── Configuration ─────────────────────────────────────────────────

struct StressConfig {
    int         count       = 1;
    std::string mode        = "parallel";
    bool        enable_ai   = false;
    int         duration_sec = 60;
    int         warmup_sec  = 10;
    int         base_port   = 8554;
    std::string input;
    int         width       = 1920;
    int         height      = 1080;
    int         fps         = 30;
    int         bitrate     = 2000000;
    std::string json_out;
};

// ── Per-session snapshot ──────────────────────────────────────────

struct MetricSnapshot {
    double   elapsed_sec = 0;
    int64_t  frames_processed = 0;
    int64_t  frames_dropped = 0;
    int      decode_ring_fill = 0;
    int      process_ring_fill = 0;
    int      max_decode_ring_fill = 0;
    int      max_process_ring_fill = 0;
    int64_t  backpressure_events = 0;
    double   eventloop_latency_us = 0;
    int      eventloop_active_fds = 0;
};

struct SessionResult {
    std::string stream_id;
    std::vector<MetricSnapshot> history;
};

// ── Helpers ───────────────────────────────────────────────────────

static long ReadVmRSS() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::istringstream iss(line.substr(6));
            long kb = 0;
            iss >> kb;
            return kb;
        }
    }
    return 0;
}

static double Percentile(const std::vector<int64_t>& sorted, double pct) {
    if (sorted.empty()) return 0;
    size_t idx = (size_t)(sorted.size() * pct / 100.0);
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return (double)sorted[idx];
}

// ── StressRunner ──────────────────────────────────────────────────

class StressRunner {
public:
    StressRunner(const StressConfig& cfg) : cfg_(cfg) {}

    int Run() {
        std::cerr << "[stress] Starting " << cfg_.count << " sessions"
                  << "  mode=" << cfg_.mode
                  << "  ai=" << (cfg_.enable_ai ? "on" : "off")
                  << "  duration=" << cfg_.duration_sec << "s"
                  << "  warmup=" << cfg_.warmup_sec << "s"
                  << std::endl;

        observe::LatencyTracer::Instance().SetLogPath("/tmp/stress_latency.jsonl");
        observe::LatencyTracer::Instance().Enable(true);

        // One process-level RTSP server shared by all streams.
        ffmpeg::StreamServer server;
        if (!server.Start("0.0.0.0", cfg_.base_port)) {
            std::cerr << "[stress] StreamServer bind failed on port "
                      << cfg_.base_port << std::endl;
            return 1;
        }

        // Create sessions
        for (int i = 0; i < cfg_.count; ++i) {
            ffmpeg::StreamSessionConfig sc;
            sc.input_url       = cfg_.input;
            sc.width           = cfg_.width;
            sc.height          = cfg_.height;
            sc.fps             = cfg_.fps;
            sc.bitrate         = cfg_.bitrate;
            sc.rtsp_suffix     = "stress_" + std::to_string(i);
            sc.http_port       = 0;
            sc.enable_ai       = cfg_.enable_ai;
            sc.enable_audio    = false;
            sc.pipeline_mode   = cfg_.mode;
            sc.enc_threads     = 1;
            sc.reconnect_on_eof = false;
            sc.enable_client_gating = false;
            sc.ringbuf_size    = 4;
            sc.max_frame_age_ms = 500;

            auto session = std::make_shared<ffmpeg::StreamSession>(sc);
            if (!session->Start(&server)) {
                std::cerr << "[stress] Session " << i << " start failed" << std::endl;
                return 1;
            }
            sessions_.push_back(session);
            std::cerr << "[stress] Session " << i
                      << " started on suffix /" << sc.rtsp_suffix << std::endl;
        }

        // Warmup
        if (cfg_.warmup_sec > 0) {
            std::cerr << "[stress] Warming up " << cfg_.warmup_sec << "s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(cfg_.warmup_sec));
        }

        // Collection loop
        int64_t start_time = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        std::vector<SessionResult> results(cfg_.count);
        for (int i = 0; i < cfg_.count; ++i) {
            results[i].stream_id = "stress_" + std::to_string(i);
        }

        long memory_peak_kb = 0;

        for (int elapsed = 0; elapsed < cfg_.duration_sec; ++elapsed) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            for (int i = 0; i < cfg_.count; ++i) {
                auto status = sessions_[i]->GetStatus();
                MetricSnapshot snap;
                snap.elapsed_sec        = elapsed + 1;
                snap.frames_processed   = status.frames_processed;
                snap.frames_dropped     = status.frames_dropped;
                snap.decode_ring_fill   = status.decode_ring_fill;
                snap.process_ring_fill  = status.process_ring_fill;
                snap.max_decode_ring_fill  = status.max_decode_ring_fill;
                snap.max_process_ring_fill = status.max_process_ring_fill;
                snap.backpressure_events   = status.backpressure_events;
                snap.eventloop_latency_us  = status.avg_eventloop_latency_us;
                snap.eventloop_active_fds  = status.eventloop_active_fds;
                results[i].history.push_back(snap);
            }

            long rss = ReadVmRSS();
            if (rss > memory_peak_kb) memory_peak_kb = rss;

            if ((elapsed + 1) % 10 == 0) {
                std::cerr << "[stress] " << (elapsed + 1) << "/"
                          << cfg_.duration_sec << "s  RSS="
                          << (rss / 1024) << "MB" << std::endl;
            }
        }

        int64_t end_time = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        double actual_duration = end_time - start_time;

        // Stop sessions
        for (auto& s : sessions_) {
            s->Stop();
        }
        server.Stop();

        // Collect latency stats
        auto latency_stats = observe::LatencyTracer::Instance().GetStats();

        OutputJson(results, actual_duration, memory_peak_kb, latency_stats);

        observe::LatencyTracer::Instance().Enable(false);
        return 0;
    }

private:
    void OutputJson(const std::vector<SessionResult>& results,
                    double duration,
                    long memory_peak_kb,
                    const std::map<std::string, observe::LatencyStatsEntry>& latency_stats) {
        std::ostream* out = &std::cout;
        std::ofstream fout;
        if (!cfg_.json_out.empty()) {
            fout.open(cfg_.json_out);
            out = &fout;
        }

        *out << "{" << std::endl;
        *out << "  \"config\": {" << std::endl;
        *out << "    \"count\": " << cfg_.count << "," << std::endl;
        *out << "    \"mode\": \"" << cfg_.mode << "\"," << std::endl;
        *out << "    \"ai_enabled\": " << (cfg_.enable_ai ? "true" : "false") << "," << std::endl;
        *out << "    \"duration_sec\": " << cfg_.duration_sec << "," << std::endl;
        *out << "    \"warmup_sec\": " << cfg_.warmup_sec << "," << std::endl;
        *out << "    \"video\": {\"width\": " << cfg_.width
             << ", \"height\": " << cfg_.height
             << ", \"fps\": " << cfg_.fps
             << ", \"bitrate\": " << cfg_.bitrate << "}" << std::endl;
        *out << "  }," << std::endl;
        *out << "  \"actual_duration_sec\": " << duration << "," << std::endl;
        *out << "  \"memory_peak_rss_kb\": " << memory_peak_kb << "," << std::endl;

        // Latency stats
        *out << "  \"latency_tracer\": {" << std::endl;
        size_t li = 0;
        for (const auto& kv : latency_stats) {
            const auto& st = kv.second;
            std::vector<int64_t> sorted = st.durations;
            std::sort(sorted.begin(), sorted.end());
            *out << "    \"" << kv.first << "\": {" << std::endl;
            *out << "      \"count\": " << st.count << "," << std::endl;
            *out << "      \"avg_us\": " << (st.count > 0 ? st.total_us / st.count : 0) << "," << std::endl;
            *out << "      \"min_us\": " << st.min_us << "," << std::endl;
            *out << "      \"max_us\": " << st.max_us << "," << std::endl;
            *out << "      \"p50_us\": " << Percentile(sorted, 50) << "," << std::endl;
            *out << "      \"p95_us\": " << Percentile(sorted, 95) << "," << std::endl;
            *out << "      \"p99_us\": " << Percentile(sorted, 99) << std::endl;
            *out << "    }";
            if (++li < latency_stats.size()) *out << ",";
            *out << std::endl;
        }
        *out << "  }," << std::endl;

        // Per-session
        *out << "  \"sessions\": [" << std::endl;
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            const auto& last = r.history.empty() ? MetricSnapshot{} : r.history.back();
            *out << "    {" << std::endl;
            *out << "      \"stream_id\": \"" << r.stream_id << "\"," << std::endl;
            *out << "      \"total_frames\": " << last.frames_processed << "," << std::endl;
            *out << "      \"total_dropped\": " << last.frames_dropped << "," << std::endl;
            *out << "      \"actual_fps\": " << (duration > 0 ? last.frames_processed / duration : 0) << "," << std::endl;
            *out << "      \"max_decode_ring_fill\": " << last.max_decode_ring_fill << "," << std::endl;
            *out << "      \"max_process_ring_fill\": " << last.max_process_ring_fill << "," << std::endl;
            *out << "      \"backpressure_events\": " << last.backpressure_events << "," << std::endl;
            *out << "      \"eventloop_latency_us\": " << last.eventloop_latency_us << "," << std::endl;
            *out << "      \"eventloop_active_fds\": " << last.eventloop_active_fds << std::endl;
            *out << "    }";
            if (i + 1 < results.size()) *out << ",";
            *out << std::endl;
        }
        *out << "  ]," << std::endl;

        // Aggregate
        int64_t total_frames = 0, total_dropped = 0;
        double total_fps = 0;
        for (const auto& r : results) {
            const auto& last = r.history.empty() ? MetricSnapshot{} : r.history.back();
            total_frames += last.frames_processed;
            total_dropped += last.frames_dropped;
            total_fps += (duration > 0 ? last.frames_processed / duration : 0);
        }
        *out << "  \"aggregate\": {" << std::endl;
        *out << "    \"total_frames_decoded\": " << total_frames << "," << std::endl;
        *out << "    \"total_frames_dropped\": " << total_dropped << "," << std::endl;
        *out << "    \"avg_fps_per_stream\": " << (results.empty() ? 0 : total_fps / results.size()) << "," << std::endl;
        *out << "    \"drop_rate_pct\": " << (total_frames > 0 ? 100.0 * total_dropped / total_frames : 0) << std::endl;
        *out << "  }" << std::endl;
        *out << "}" << std::endl;
    }

    StressConfig cfg_;
    std::vector<std::shared_ptr<ffmpeg::StreamSession>> sessions_;
};

// ── CLI ─────────────────────────────────────────────────────────────

static void PrintUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]" << std::endl
              << "  --count N         Concurrent streams (default: 1)" << std::endl
              << "  --mode MODE       serial|parallel (default: parallel)" << std::endl
              << "  --enable-ai       Enable AI processing" << std::endl
              << "  --duration SEC    Test duration in seconds (default: 60)" << std::endl
              << "  --warmup SEC      Warmup duration in seconds (default: 10)" << std::endl
              << "  --base-port PORT  Starting RTSP port (default: 8554)" << std::endl
              << "  --input FILE      Video file path (required)" << std::endl
              << "  --width W         Output width (default: 1920)" << std::endl
              << "  --height H        Output height (default: 1080)" << std::endl
              << "  --fps FPS         Target fps (default: 30)" << std::endl
              << "  --bitrate BPS     Encoder bitrate (default: 2000000)" << std::endl
              << "  --json-out FILE   JSON output file (default: stdout)" << std::endl;
}

int main(int argc, char** argv) {
    StressConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--count"       && i + 1 < argc) cfg.count        = std::stoi(argv[++i]);
        else if (a == "--mode"   && i + 1 < argc) cfg.mode         = argv[++i];
        else if (a == "--enable-ai")               cfg.enable_ai   = true;
        else if (a == "--duration" && i + 1 < argc) cfg.duration_sec = std::stoi(argv[++i]);
        else if (a == "--warmup"  && i + 1 < argc) cfg.warmup_sec  = std::stoi(argv[++i]);
        else if (a == "--base-port" && i + 1 < argc) cfg.base_port = std::stoi(argv[++i]);
        else if (a == "--input"   && i + 1 < argc) cfg.input       = argv[++i];
        else if (a == "--width"   && i + 1 < argc) cfg.width       = std::stoi(argv[++i]);
        else if (a == "--height"  && i + 1 < argc) cfg.height      = std::stoi(argv[++i]);
        else if (a == "--fps"     && i + 1 < argc) cfg.fps         = std::stoi(argv[++i]);
        else if (a == "--bitrate" && i + 1 < argc) cfg.bitrate     = std::stoi(argv[++i]);
        else if (a == "--json-out" && i + 1 < argc) cfg.json_out   = argv[++i];
        else if (a == "--help") { PrintUsage(argv[0]); return 0; }
    }

    if (cfg.input.empty()) {
        std::cerr << "[stress] --input is required" << std::endl;
        return 1;
    }

    StressRunner runner(cfg);
    return runner.Run();
}
