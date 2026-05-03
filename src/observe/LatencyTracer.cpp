#include "observe/LatencyTracer.h"

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace observe {

// ─── LatencyScope ──────────────────────────────────────────────────────────────

LatencyScope::LatencyScope(const std::string& module, const std::string& stage,
                           const std::string& stream_id, int64_t frame_id)
    : enabled_(LatencyTracer::Instance().IsEnabled())
    , module_(module)
    , stage_(stage)
    , stream_id_(stream_id)
    , frame_id_(frame_id)
    , start_us_(0)
{
    if (enabled_) {
        start_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

LatencyScope::~LatencyScope() {
    if (!enabled_) return;

    auto now = std::chrono::steady_clock::now();
    int64_t end_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();

    LatencyEvent ev;
    ev.module    = module_;
    ev.stage     = stage_;
    ev.event     = "scope";
    ev.stream_id = stream_id_;
    ev.frame_id  = frame_id_;
    ev.start_us  = start_us_;
    ev.end_us    = end_us;
    ev.duration_us = end_us - start_us_;
    ev.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ev.thread_id = LatencyTracer::GetThreadId();

    LatencyTracer::Instance().LogEvent(ev);
}

// ─── LatencyTracer ─────────────────────────────────────────────────────────────

LatencyTracer& LatencyTracer::Instance() {
    static LatencyTracer inst;
    return inst;
}

LatencyTracer::LatencyTracer() {
    InitFromEnv();
}

LatencyTracer::~LatencyTracer() {
    Flush();

    writer_running_.store(false);
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        write_queue_.clear();
    }
    queue_cv_.notify_all();
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    if (write_fd_ >= 0) {
        ::close(write_fd_);
        write_fd_ = -1;
    }
}

void LatencyTracer::Enable(bool on) {
    enabled_.store(on);
    if (on && !writer_running_.load()) {
        EnsureLogDir();
        write_fd_ = ::open(log_path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (write_fd_ < 0) {
            std::cerr << "[LatencyTracer] Cannot open log: " << log_path_ << std::endl;
            enabled_.store(false);
            return;
        }
        writer_running_.store(true);
        writer_thread_ = std::thread(&LatencyTracer::WriterThreadFunc, this);
    }
}

void LatencyTracer::SetLogPath(const std::string& path) {
    log_path_ = path;
}

void LatencyTracer::SetBufferSize(size_t n) {
    buffer_size_ = n;
    std::lock_guard<std::mutex> lk(ring_mutex_);
    while (ring_buffer_.size() > buffer_size_) {
        ring_buffer_.pop_front();
    }
}

void LatencyTracer::InitFromEnv() {
    const char* en = std::getenv("STREAMSIGHT_LATENCY_ENABLE");
    if (en && (std::string(en) == "1" || std::string(en) == "true")) {
        const char* log = std::getenv("STREAMSIGHT_LATENCY_LOG");
        if (log && log[0]) {
            log_path_ = log;
        } else {
            log_path_ = "runtime/latency_events.jsonl";
        }

        const char* bs = std::getenv("STREAMSIGHT_LATENCY_BUFFER_SIZE");
        if (bs && bs[0]) {
            buffer_size_ = static_cast<size_t>(std::atol(bs));
        }

        Enable(true);
    }
}

void LatencyTracer::EnsureLogDir() {
    std::string dir;
    size_t pos = log_path_.rfind('/');
    if (pos != std::string::npos) {
        dir = log_path_.substr(0, pos);
        // mkdir -p style: create intermediate dirs
        std::string accum;
        for (size_t i = 0; i < dir.size(); ++i) {
            if (dir[i] == '/') {
                if (!accum.empty()) {
                    ::mkdir(accum.c_str(), 0755);
                }
            }
            accum += dir[i];
        }
        if (!accum.empty()) {
            ::mkdir(accum.c_str(), 0755);
        }
    }
}

void LatencyTracer::LogEvent(const LatencyEvent& ev) {
    if (!enabled_.load(std::memory_order_relaxed)) return;

    // Ring buffer
    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        ring_buffer_.push_back(ev);
        while (ring_buffer_.size() > buffer_size_) {
            ring_buffer_.pop_front();
        }

        // Aggregate stats
        std::string key = ev.module + "." + ev.stage;
        auto& st = stats_[key];
        st.count++;
        st.total_us += ev.duration_us;
        if (st.count == 1 || ev.duration_us < st.min_us) st.min_us = ev.duration_us;
        if (st.count == 1 || ev.duration_us > st.max_us) st.max_us = ev.duration_us;
        st.durations.push_back(ev.duration_us);
        // Keep durations list bounded (keep last 1000 per stage for percentile)
        if (st.durations.size() > 1000) {
            st.durations.erase(st.durations.begin());
        }
    }

    // Writer queue
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        write_queue_.push_back(ev);
    }
    queue_cv_.notify_one();
}

std::vector<LatencyEvent> LatencyTracer::GetRecentEvents(int limit) const {
    std::lock_guard<std::mutex> lk(ring_mutex_);
    std::vector<LatencyEvent> out;
    if (ring_buffer_.empty()) return out;

    int start = std::max(0, static_cast<int>(ring_buffer_.size()) - limit);
    out.reserve(ring_buffer_.size() - start);
    for (size_t i = static_cast<size_t>(start); i < ring_buffer_.size(); ++i) {
        out.push_back(ring_buffer_[i]);
    }
    return out;
}

std::map<std::string, LatencyStatsEntry> LatencyTracer::GetStats() const {
    std::lock_guard<std::mutex> lk(ring_mutex_);
    return stats_;  // copy
}

void LatencyTracer::Reset() {
    std::lock_guard<std::mutex> lk(ring_mutex_);
    ring_buffer_.clear();
    stats_.clear();
}

void LatencyTracer::Flush() {
    // Flush pending writes
    std::vector<LatencyEvent> pending;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        pending.swap(write_queue_);
    }
    for (const auto& ev : pending) {
        std::string line = EventToJson(ev);
        WriteLine(line);
    }
}

std::string LatencyTracer::GetThreadId() {
    std::ostringstream ss;
    ss << std::this_thread::get_id();
    return ss.str();
}

// ─── JSON helpers ──────────────────────────────────────────────────────────────

std::string LatencyTracer::JsonEscape(const std::string& s, bool quote) {
    std::string out;
    out.reserve(s.size() + 4);
    if (quote) out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    if (quote) out += '"';
    return out;
}

std::string LatencyTracer::EventToJson(const LatencyEvent& ev) const {
    std::ostringstream ss;
    ss << "{"
       << "\"timestamp_ms\":" << ev.timestamp_ms
       << ",\"trace_id\":" << JsonEscape(ev.trace_id)
       << ",\"stream_id\":" << JsonEscape(ev.stream_id)
       << ",\"frame_id\":" << ev.frame_id
       << ",\"module\":" << JsonEscape(ev.module)
       << ",\"stage\":" << JsonEscape(ev.stage)
       << ",\"event\":" << JsonEscape(ev.event)
       << ",\"start_us\":" << ev.start_us
       << ",\"end_us\":" << ev.end_us
       << ",\"duration_us\":" << ev.duration_us
       << ",\"thread_id\":" << JsonEscape(ev.thread_id);

    if (!ev.extra.empty()) {
        ss << ",\"extra\":" << ev.extra;
    }

    ss << "}";
    return ss.str();
}

// ─── Background writer ─────────────────────────────────────────────────────────

void LatencyTracer::WriterThreadFunc() {
    std::vector<LatencyEvent> batch;
    while (writer_running_.load(std::memory_order_relaxed)) {
        {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            queue_cv_.wait_for(lk, std::chrono::milliseconds(500), [this]() {
                return !write_queue_.empty() || !writer_running_.load();
            });
            if (write_queue_.empty() && !writer_running_.load()) break;
            batch.swap(write_queue_);
        }

        for (const auto& ev : batch) {
            std::string line = EventToJson(ev);
            WriteLine(line);
        }
        batch.clear();
    }

    // Final flush
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        if (!write_queue_.empty()) {
            batch.swap(write_queue_);
        }
    }
    for (const auto& ev : batch) {
        std::string line = EventToJson(ev);
        WriteLine(line);
    }
}

void LatencyTracer::WriteLine(const std::string& line) {
    if (write_fd_ < 0) return;
    ssize_t total = static_cast<ssize_t>(line.size());
    const char* data = line.data();
    while (total > 0) {
        ssize_t n = ::write(write_fd_, data, static_cast<size_t>(total));
        if (n <= 0) break;
        data  += n;
        total -= n;
    }
    // Append newline
    ::write(write_fd_, "\n", 1);
}

} // namespace observe
