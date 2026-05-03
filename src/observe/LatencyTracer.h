#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace observe {

struct LatencyEvent {
    int64_t     timestamp_ms = 0;
    std::string trace_id;
    std::string stream_id;
    int64_t     frame_id = 0;
    std::string module;
    std::string stage;
    std::string event;       // "scope" or "mark"
    int64_t     start_us = 0;
    int64_t     end_us   = 0;
    int64_t     duration_us = 0;
    std::string thread_id;
    std::string extra;       // optional JSON fragment
};

struct LatencyStatsEntry {
    int64_t count       = 0;
    int64_t total_us    = 0;
    int64_t min_us      = 0;
    int64_t max_us      = 0;
    std::vector<int64_t> durations;
};

class LatencyScope {
public:
    LatencyScope(const std::string& module, const std::string& stage,
                 const std::string& stream_id = "", int64_t frame_id = 0);
    ~LatencyScope();

    LatencyScope(const LatencyScope&) = delete;
    LatencyScope& operator=(const LatencyScope&) = delete;

private:
    bool        enabled_;
    std::string module_;
    std::string stage_;
    std::string stream_id_;
    int64_t     frame_id_;
    int64_t     start_us_;
};

class LatencyTracer {
public:
    static LatencyTracer& Instance();

    void Enable(bool on);
    bool IsEnabled() const { return enabled_.load(std::memory_order_relaxed); }

    void SetLogPath(const std::string& path);
    void SetBufferSize(size_t n);

    void LogEvent(const LatencyEvent& ev);

    std::vector<LatencyEvent> GetRecentEvents(int limit = 100) const;
    std::map<std::string, LatencyStatsEntry> GetStats() const;

    void Reset();
    void Flush();

    // Helper for generating thread id strings.
    static std::string GetThreadId();

private:
    LatencyTracer();
    ~LatencyTracer();

    LatencyTracer(const LatencyTracer&) = delete;
    LatencyTracer& operator=(const LatencyTracer&) = delete;

    void InitFromEnv();
    void EnsureLogDir();
    std::string EventToJson(const LatencyEvent& ev) const;
    static std::string JsonEscape(const std::string& s, bool quote = true);

    void WriterThreadFunc();
    void WriteLine(const std::string& line);

    std::atomic<bool> enabled_{false};
    std::string       log_path_;
    size_t            buffer_size_ = 10000;

    // Ring buffer + aggregated stats (mutex protected)
    mutable std::mutex                           ring_mutex_;
    std::deque<LatencyEvent>                     ring_buffer_;
    std::map<std::string, LatencyStatsEntry>     stats_;

    // Background writer queue
    std::mutex                     queue_mutex_;
    std::condition_variable        queue_cv_;
    std::vector<LatencyEvent>      write_queue_;
    std::thread                    writer_thread_;
    std::atomic<bool>              writer_running_{false};
    int                            write_fd_ = -1;
};

} // namespace observe

// ─── Convenience macros ────────────────────────────────────────────────────────
// Usage: place at the top of a function or block scope.
// The LatencyScope destructor auto-submits the duration.
//
// Example:
//   void Foo() {
//       STREAMSIGHT_LATENCY_SCOPE("ai", "face_detection");
//       ...
//   }

#define STREAMSIGHT_LATENCY_SCOPE(mod, stg) \
    observe::LatencyScope _latency_scope_##__LINE__(mod, stg)

#define STREAMSIGHT_LATENCY_SCOPE_WITH_IDS(mod, stg, sid, fid) \
    observe::LatencyScope _latency_scope_##__LINE__(mod, stg, sid, fid)

#define STREAMSIGHT_LATENCY_MARK(mod, stg, evt) \
    do { \
        ::observe::LatencyTracer& _t = ::observe::LatencyTracer::Instance(); \
        if (_t.IsEnabled()) { \
            ::observe::LatencyEvent _ev; \
            _ev.module = mod; \
            _ev.stage  = stg; \
            _ev.event  = evt; \
            auto _now = std::chrono::system_clock::now(); \
            _ev.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>( \
                _now.time_since_epoch()).count(); \
            _ev.thread_id = ::observe::LatencyTracer::GetThreadId(); \
            _t.LogEvent(_ev); \
        } \
    } while(0)

#define STREAMSIGHT_LATENCY_MARK_WITH_IDS(mod, stg, evt, sid, fid) \
    do { \
        ::observe::LatencyTracer& _t = ::observe::LatencyTracer::Instance(); \
        if (_t.IsEnabled()) { \
            ::observe::LatencyEvent _ev; \
            _ev.module    = mod; \
            _ev.stage     = stg; \
            _ev.event     = evt; \
            _ev.stream_id = sid; \
            _ev.frame_id  = fid; \
            auto _now = std::chrono::system_clock::now(); \
            _ev.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>( \
                _now.time_since_epoch()).count(); \
            _ev.thread_id = ::observe::LatencyTracer::GetThreadId(); \
            _t.LogEvent(_ev); \
        } \
    } while(0)
