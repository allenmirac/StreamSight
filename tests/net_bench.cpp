// net_bench.cpp — network-layer microbenchmark for StreamSight.
//
// Exercises src/net/ directly (no FFmpeg/OpenCV), measuring:
//   1. RingBuffer SPSC throughput   — the intra-process hand-off primitive
//   2. TCP connect churn            — basic connect/disconnect rate
//   3. Concurrent connection hold   — how many live connections the loop holds,
//                                     and how it behaves while they stay open
//   4. Server-send throughput       — aggregate bytes/s the loop pushes out
//   5. EventLoop dispatch latency   — GetAvgLoopUs
//
// Usage:
//   ./build/bin/streamsight-netbench [--bench all|ring|churn|conn|bw|loop]
//        [--connects 10000] [--threads 2]        # churn: total, client threads
//        [--hold "1000,5000,10000"] [--hold-secs 10]
//        [--bw-conns 64] [--bw-payload 1400] [--bw-duration 10] [--bw-tick-ms 1]
//        [--ring-ops 5000000] [--ring-capacity 64]
//        [--loop-threads 1] [--port 19999] [--json-out FILE]
//
// Measurement notes (these are the reasons several numbers look the way they do):
//   * churn's rate denominator is the slowest client thread (all threads join),
//     so it is straggler-sensitive. Per-thread wall times are reported as
//     percentiles so a single unlucky thread is visible instead of hidden.
//   * ConnectOnce() sets SO_LINGER(0) so close() sends RST instead of entering
//     TIME_WAIT; without it a connect storm exhausts the client's ephemeral
//     ports and the measured "collapse" is a client artifact. The cost is that
//     some connections are RST before the server accepts them, which shows up
//     as accepted < clients_ok — reported separately, never folded away.
//   * bw's headline number is the bytes the *client verified it received*, not
//     the bytes handed to TcpConnection::Send(). Send() ignores the return of
//     BufferWriter::Append(), which fails once a connection's write queue is
//     full (500 packets), so submitted != delivered under backpressure.
//
// Build with -O2 (Release) for meaningful numbers.

#include "net/EventLoop.h"
#include "net/TcpServer.h"
#include "net/TcpConnection.h"
#include "net/RingBuffer.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using streamsight::net::EventLoop;
using streamsight::net::RingBuffer;
using streamsight::net::TcpConnection;
using streamsight::net::TcpServer;
using streamsight::net::TimerId;

namespace {

int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Read a VmXxx field (kB) from /proc/self/status.
int64_t ProcStatusKb(const char* key) {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(key, 0) == 0) {
            std::istringstream is(line.substr(std::strlen(key)));
            int64_t kb = 0;
            is >> kb;
            return kb;
        }
    }
    return 0;
}

// Open file descriptor count, for cross-checking against the connection count.
int64_t CountOpenFds() {
    DIR* d = ::opendir("/proc/self/fd");
    if (!d) return 0;
    int64_t n = 0;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] != '.') ++n;
    }
    ::closedir(d);
    return n;
}

// Nearest-rank percentile over an already-sorted vector.
double Percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const size_t n = sorted.size();
    size_t idx = static_cast<size_t>(p * static_cast<double>(n - 1) + 0.5);
    if (idx >= n) idx = n - 1;
    return sorted[idx];
}

std::vector<int> ParseIntList(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(std::stoi(item));
    }
    if (out.empty()) out.push_back(std::stoi(s));
    return out;
}

// Accept-counting server. Counts on the event-loop thread inside OnConnect,
// so `accepted` reflects connections the loop actually processed.
//
// Optionally keeps a registry of live connections so a driver can push bytes
// to them; tracking is opt-in because holding the shared_ptrs would otherwise
// keep dead connections (and their memory) alive during the churn test.
class BenchServer : public TcpServer {
public:
    explicit BenchServer(EventLoop* loop) : TcpServer(loop) {}

    std::atomic<int64_t> accepted{0};

    void SetTrackConnections(bool on) {
        std::lock_guard<std::mutex> lock(conns_mutex_);
        track_conns_ = on;
        if (!on) conns_.clear();
    }

    template <typename F>
    void ForEachTrackedConnection(F&& fn) {
        std::lock_guard<std::mutex> lock(conns_mutex_);
        for (auto& c : conns_) fn(c);
    }

protected:
    TcpConnection::Ptr OnConnect(SOCKET sockfd) override {
        accepted.fetch_add(1, std::memory_order_relaxed);
        TcpConnection::Ptr conn = TcpServer::OnConnect(sockfd);
        std::lock_guard<std::mutex> lock(conns_mutex_);
        if (track_conns_) conns_.push_back(conn);
        return conn;
    }

private:
    std::mutex conns_mutex_;
    bool track_conns_ = false;
    std::vector<TcpConnection::Ptr> conns_;
};

// Connect (and immediately close) one TCP connection. Returns the fd on
// success, -1 on failure. Caller closes when holding.
//
// SO_LINGER(0) makes close() emit RST instead of entering TIME_WAIT. Without
// it a connect storm exhausts the client's ephemeral-port range (TIME_WAIT
// lasts 60s, ~28k ports) and the measured rate collapses for reasons that
// have nothing to do with the server under test.
int ConnectOnce(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct linger lg{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void WaitAccepted(const BenchServer& srv, int64_t target, int timeout_ms) {
    const int64_t t0 = NowUs();
    while (srv.accepted.load(std::memory_order_relaxed) < target &&
           (NowUs() - t0) / 1000 < timeout_ms) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

struct Result {
    std::string name;
    double      value;
    std::string unit;
    std::string note;
};

void AddResult(std::vector<Result>& v, const std::string& n, double val,
               const std::string& u, const std::string& note = "") {
    v.push_back({n, val, u, note});
}

// ── 1. RingBuffer SPSC throughput ───────────────────────────────────────────
// `overwrite` selects the mutex-guarded PushOrDrop path the pipeline uses
// (backpressure-aware) instead of the lock-free Push path.
//
// PushOrDrop never blocks the producer — when the ring is full it drops an
// item (OldDropped/NewDropped) — so throughput is measured producer-side.
// Push retries until it fits, so both paths are timed over `ops` producer
// iterations with a consumer draining in parallel.
Result BenchRing(int capacity, int64_t ops, bool overwrite) {
    RingBuffer<int> rb(capacity);
    auto ts = [](const int&) -> int64_t { return 0; };
    std::atomic<bool> prod_done{false};

    const int64_t t0 = NowUs();
    std::thread consumer([&] {
        int v = 0;
        while (!prod_done.load(std::memory_order_relaxed) || !rb.IsEmpty()) {
            if (!rb.Pop(v)) std::this_thread::yield();
        }
    });

    for (int64_t i = 0; i < ops; ++i) {
        if (overwrite) {
            rb.PushOrDrop(static_cast<int>(i), 0, 1, ts);
        } else {
            while (!rb.Push(static_cast<int>(i)))
                std::this_thread::yield();  // ring full — spin
        }
    }
    const double us = static_cast<double>(NowUs() - t0);
    prod_done.store(true, std::memory_order_relaxed);
    consumer.join();

    const std::string base = overwrite ? "ringbuf_pushdrop_" : "ringbuf_spsc_";
    // ops per second / 1e6 == ops / us
    return {base + std::to_string(capacity),
            static_cast<double>(ops) / us, "Mops/s",
            "cap=" + std::to_string(capacity)};
}

// ── 2. TCP connect churn (basic test) ───────────────────────────────────────
// Connection-establishment rate: client-observed connects per second.
//
// Timing stops when the client threads finish, NOT when the server counter
// reaches the target — otherwise a handful of connections the server never
// accepts (RST arrives first) would inflate the window to the full timeout
// and depress the rate for reasons unrelated to throughput. The server counter
// is still drained and reported so losses stay visible.
struct ChurnResult {
    double  rate = 0.0;         // clients_ok / slowest-thread elapsed
    int64_t clients_ok = 0;     // connects the client saw succeed
    int64_t accepted = 0;       // connects the server loop processed
    int64_t target = 0;         // connects attempted
    double  p50_us = 0.0;       // per-thread wall time distribution
    double  p99_us = 0.0;
    double  max_us = 0.0;
};

ChurnResult StormAccepts(BenchServer& srv, uint16_t port, int threads,
                         int per_thread) {
    const int64_t before = srv.accepted.load(std::memory_order_relaxed);
    std::vector<int64_t> per_ok(static_cast<size_t>(threads), 0);
    std::vector<double>  per_us(static_cast<size_t>(threads), 0.0);
    std::atomic<bool> go{false};

    std::vector<std::thread> ts;
    ts.reserve(static_cast<size_t>(threads));
    for (int t = 0; t < threads; ++t) {
        ts.emplace_back([&, t] {
            while (!go.load(std::memory_order_relaxed))
                std::this_thread::yield();
            const int64_t t0 = NowUs();
            int64_t ok = 0;
            for (int i = 0; i < per_thread; ++i) {
                int fd = ConnectOnce(port);
                if (fd >= 0) { ++ok; ::close(fd); }
            }
            per_us[static_cast<size_t>(t)] =
                static_cast<double>(NowUs() - t0);
            per_ok[static_cast<size_t>(t)] = ok;
        });
    }

    const int64_t t0 = NowUs();
    go.store(true, std::memory_order_relaxed);
    for (auto& th : ts) th.join();
    const double us = static_cast<double>(NowUs() - t0);

    // Bounded settle: let the server count what it accepted. Not timed.
    const int64_t target = before + static_cast<int64_t>(threads) * per_thread;
    const int64_t s0 = NowUs();
    while (srv.accepted.load(std::memory_order_relaxed) < target &&
           NowUs() - s0 < 1000000) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    ChurnResult r;
    for (auto v : per_ok) r.clients_ok += v;
    r.accepted = srv.accepted.load(std::memory_order_relaxed) - before;
    r.target   = target - before;
    r.rate     = us > 0 ? static_cast<double>(r.clients_ok) / us * 1e6 : 0.0;

    std::sort(per_us.begin(), per_us.end());
    r.p50_us = Percentile(per_us, 0.50);
    r.p99_us = Percentile(per_us, 0.99);
    r.max_us = per_us.empty() ? 0.0 : per_us.back();
    return r;
}

// ── 3. Concurrent connection hold ───────────────────────────────────────────
// Opens `count` connections, holds them for `hold_sec` while sampling memory,
// then reports how many were still alive at the end. The liveness probe uses
// MSG_PEEK: 0 means the peer closed, EAGAIN means the connection is idle but
// alive (the server sends nothing in this benchmark).
struct ConnResult {
    int64_t requested = 0;
    int64_t opened = 0;
    int64_t accepted = 0;
    int64_t alive_end = 0;
    int64_t fd_count = 0;
    int64_t rss_start_kb = 0;
    int64_t rss_end_kb = 0;
    int64_t rss_peak_kb = 0;
    double  hold_sec = 0.0;
    bool    aborted = false;      // hit a connect failure before reaching count
};

ConnResult BenchConn(BenchServer& srv, uint16_t port, int count, int hold_sec) {
    ConnResult r;
    r.requested = count;
    r.hold_sec  = hold_sec;
    r.rss_start_kb = ProcStatusKb("VmRSS:");
    r.rss_peak_kb  = r.rss_start_kb;

    const int64_t before = srv.accepted.load(std::memory_order_relaxed);
    std::vector<int> fds;
    fds.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        int fd = ConnectOnce(port);
        if (fd < 0) { r.aborted = true; break; }   // stop at the first failure
        fds.push_back(fd);
    }
    r.opened = static_cast<int64_t>(fds.size());
    WaitAccepted(srv, before + r.opened, 10000);

    // Hold, sampling once a second so a slow climb is visible in the peak.
    for (int s = 0; s < hold_sec; ++s) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        r.rss_peak_kb = std::max(r.rss_peak_kb, ProcStatusKb("VmRSS:"));
    }

    int64_t alive = 0;
    for (int fd : fds) {
        char c = 0;
        const ssize_t n = ::recv(fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
        const bool live = (n > 0) ||
                          (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        if (live) ++alive;
    }
    r.alive_end  = alive;
    r.fd_count   = CountOpenFds();
    r.rss_end_kb = ProcStatusKb("VmRSS:");
    r.rss_peak_kb = std::max(r.rss_peak_kb, r.rss_end_kb);
    r.accepted   = srv.accepted.load(std::memory_order_relaxed) - before;

    for (int fd : fds) ::close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return r;
}

// ── 4. Server-send throughput ───────────────────────────────────────────────
// An EventLoop timer pushes `payload` bytes to every tracked connection once
// per `tick_ms`; `ndrain` threads read as fast as they can on the client side.
//
// The headline number is what the clients verified they received. What the
// server handed to Send() is reported too, but it is an upper bound: Send()
// ignores BufferWriter::Append()'s failure, so once a connection's write queue
// (~500 packets) is full the excess is dropped on the floor without a signal.
struct BwShared {
    std::atomic<int64_t> tx{0};
    std::atomic<int64_t> rx{0};
    std::atomic<bool>    stop{false};
    std::vector<char>    payload;
};

struct BwResult {
    int64_t conns = 0;
    int64_t payload = 0;
    int64_t tick_ms = 0;
    double  elapsed_s = 0.0;
    int64_t tx_bytes = 0;        // handed to Send() (upper bound)
    int64_t rx_bytes = 0;        // verified received by clients
    double  rx_mbps = 0.0;       // headline
    double  tx_mbps = 0.0;
    int64_t undelivered_bytes = 0;   // tx - rx after a full drain
    int64_t rss_start_kb = 0;
    int64_t rss_end_kb = 0;
    int64_t rss_peak_kb = 0;
};

void DrainWorker(std::vector<int> fds, std::shared_ptr<BwShared> sh) {
    std::vector<pollfd> pfds(fds.size());
    for (size_t i = 0; i < fds.size(); ++i) {
        pfds[i].fd = fds[i];
        pfds[i].events = POLLIN;
    }
    std::vector<char> buf(64 * 1024);

    auto pump = [&](bool until_idle) {
        for (auto& p : pfds) {
            if (p.fd < 0) continue;
            for (;;) {
                const ssize_t r = ::recv(p.fd, buf.data(), buf.size(), MSG_DONTWAIT);
                if (r > 0) {
                    sh->rx.fetch_add(r, std::memory_order_relaxed);
                    continue;
                }
                if (r == 0) p.fd = -1;      // peer closed
                break;
            }
            if (!until_idle) break;
        }
    };

    while (!sh->stop.load(std::memory_order_relaxed)) {
        const int n = ::poll(pfds.data(), pfds.size(), 100);
        if (n <= 0) continue;
        for (auto& p : pfds) {
            if (p.fd < 0) continue;
            if (!(p.revents & (POLLIN | POLLHUP | POLLERR))) continue;
            for (;;) {
                const ssize_t r = ::recv(p.fd, buf.data(), buf.size(), MSG_DONTWAIT);
                if (r > 0) {
                    sh->rx.fetch_add(r, std::memory_order_relaxed);
                    continue;
                }
                if (r == 0) p.fd = -1;  // peer closed
                break;
            }
        }
    }
    pump(/*until_idle=*/true);   // final sweep of whatever is still buffered
}

BwResult BenchBw(EventLoop& loop, BenchServer& srv, uint16_t port, int nconns,
                 int payload, int duration_s, int tick_ms, int ndrain) {
    BwResult r;
    r.payload = payload;
    r.tick_ms = tick_ms;
    r.rss_start_kb = ProcStatusKb("VmRSS:");
    r.rss_peak_kb  = r.rss_start_kb;

    srv.SetTrackConnections(true);
    const int64_t before = srv.accepted.load(std::memory_order_relaxed);

    std::vector<int> fds;
    fds.reserve(static_cast<size_t>(nconns));
    for (int i = 0; i < nconns; ++i) {
        int fd = ConnectOnce(port);
        if (fd < 0) break;
        fds.push_back(fd);
    }
    r.conns = static_cast<int64_t>(fds.size());
    WaitAccepted(srv, before + r.conns, 10000);

    auto sh = std::make_shared<BwShared>();
    sh->payload.assign(static_cast<size_t>(payload), 'x');

    const TimerId tid = loop.AddTimer([sh, &srv]() -> bool {
        int64_t n = 0;
        srv.ForEachTrackedConnection([&](const TcpConnection::Ptr& c) {
            if (c->IsClosed()) return;
            c->Send(sh->payload.data(),
                    static_cast<uint32_t>(sh->payload.size()));
            n += static_cast<int64_t>(sh->payload.size());
        });
        sh->tx.fetch_add(n, std::memory_order_relaxed);
        return true;   // repeat
    }, static_cast<uint32_t>(tick_ms));

    // TaskScheduler::AddTimer() does not write to the wakeup pipe, so a timer
    // added while the loop is parked in epoll_wait(-1) will not fire until
    // unrelated I/O happens to wake it. Nudge the loop the same way any other
    // cross-thread AddTriggerEvent would. (See PLAN §8 bug list.)
    loop.AddTriggerEvent([] {});

    std::vector<std::thread> drainers;
    const int nd = std::max(1, std::min(ndrain, static_cast<int>(fds.size())));
    for (int t = 0; t < nd; ++t) {
        std::vector<int> slice;
        for (size_t i = static_cast<size_t>(t); i < fds.size();
             i += static_cast<size_t>(nd)) {
            slice.push_back(fds[i]);
        }
        drainers.emplace_back(DrainWorker, std::move(slice), sh);
    }

    const int64_t t0 = NowUs();
    for (int s = 0; s < duration_s; ++s) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        r.rss_peak_kb = std::max(r.rss_peak_kb, ProcStatusKb("VmRSS:"));
    }
    r.elapsed_s = static_cast<double>(NowUs() - t0) / 1e6;

    loop.RemoveTimer(tid);
    // Let the loop flush whatever the write buffers still hold before the
    // drainers stop, so undelivered == actually dropped, not still-in-flight.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    sh->stop.store(true, std::memory_order_relaxed);
    for (auto& th : drainers) th.join();

    r.tx_bytes = sh->tx.load(std::memory_order_relaxed);
    r.rx_bytes = sh->rx.load(std::memory_order_relaxed);
    r.undelivered_bytes = r.tx_bytes - r.rx_bytes;
    r.rx_mbps = r.elapsed_s > 0 ? r.rx_bytes / r.elapsed_s / 1e6 : 0.0;
    r.tx_mbps = r.elapsed_s > 0 ? r.tx_bytes / r.elapsed_s / 1e6 : 0.0;
    r.rss_end_kb = ProcStatusKb("VmRSS:");
    r.rss_peak_kb = std::max(r.rss_peak_kb, r.rss_end_kb);

    for (int fd : fds) ::close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    srv.SetTrackConnections(false);
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    std::string bench     = "all";
    int64_t connects      = 20000;
    int     threads_opt   = 0;             // 0 = sweep {1,2,4,8}
    std::string holds     = "1000,5000,10000";
    int     hold_secs     = 10;
    int64_t ring_ops      = 5000000;
    int     ring_cap      = 64;
    int     loop_threads  = 1;
    uint16_t port         = 19999;
    int     bw_conns      = 64;
    int     bw_payload    = 1400;
    int     bw_duration   = 10;
    int     bw_tick_ms    = 1;
    int     bw_drainers   = 4;
    std::string json_out;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--bench")            bench = next();
        else if (a == "--connects")    connects = std::stoll(next());
        else if (a == "--threads")     threads_opt = std::stoi(next());
        else if (a == "--hold")        holds = next();
        else if (a == "--hold-secs")   hold_secs = std::stoi(next());
        else if (a == "--ring-ops")    ring_ops = std::stoll(next());
        else if (a == "--ring-capacity") ring_cap = std::stoi(next());
        else if (a == "--loop-threads") loop_threads = std::stoi(next());
        else if (a == "--port")        port = static_cast<uint16_t>(std::stoi(next()));
        else if (a == "--bw-conns")    bw_conns = std::stoi(next());
        else if (a == "--bw-payload")  bw_payload = std::stoi(next());
        else if (a == "--bw-duration") bw_duration = std::stoi(next());
        else if (a == "--bw-tick-ms")  bw_tick_ms = std::stoi(next());
        else if (a == "--bw-drainers") bw_drainers = std::stoi(next());
        else if (a == "--json-out")    json_out = next();
        else if (a == "--help") {
            std::printf("usage: %s [--bench all|ring|churn|conn|bw|loop] ...\n",
                        argv[0]);
            return 0;
        }
    }

    const bool all = (bench == "all");
    const bool run_ring  = all || bench == "ring";
    const bool run_churn = all || bench == "churn";
    const bool run_conn  = all || bench == "conn";
    const bool run_bw    = all || bench == "bw";
    const bool run_loop  = all || bench == "loop";

    std::vector<Result> results;
    std::vector<int> hold_list = ParseIntList(holds);
    const int64_t rss_start = ProcStatusKb("VmRSS:");

    // ── 1. RingBuffer ───────────────────────────────────────────────────────
    if (run_ring) {
        for (int cap : {16, ring_cap, 4096}) {
            Result r = BenchRing(cap, ring_ops, /*overwrite=*/false);
            printf("[ring] spsc  cap=%-5d %8.1f Mops/s\n", cap, r.value);
            AddResult(results, r.name, r.value, r.unit, r.note);
        }
        for (int cap : {ring_cap, 4096}) {
            Result r = BenchRing(cap, ring_ops, /*overwrite=*/true);
            printf("[ring] pushdr cap=%-5d %8.1f Mops/s\n", cap, r.value);
            AddResult(results, r.name, r.value, r.unit, r.note);
        }
    }

    // ── Server ──────────────────────────────────────────────────────────────
    EventLoop loop(static_cast<uint32_t>(loop_threads));
    BenchServer server(&loop);
    if (!server.Start("127.0.0.1", port)) {
        std::cerr << "netbench: failed to bind port " << port << std::endl;
        return 1;
    }
    // Give the loop a moment to register the listening channel.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // ── 2. TCP connect churn ────────────────────────────────────────────────
    if (run_churn) {
        std::vector<int> sweep = threads_opt > 0
                                     ? std::vector<int>{threads_opt}
                                     : std::vector<int>{1, 2, 4, 8};
        for (int th : sweep) {
            const int per = std::max(1, static_cast<int>(connects) / th);
            ChurnResult c = StormAccepts(server, port, th, per);
            printf("[churn] %2d threads %11.0f conn/s  threads/session=%d  "
                   "clients_ok=%lld accepted=%lld target=%lld lost=%lld  "
                   "thread-ms p50=%.1f p99=%.1f max=%.1f\n",
                   th, c.rate, per,
                   static_cast<long long>(c.clients_ok),
                   static_cast<long long>(c.accepted),
                   static_cast<long long>(c.target),
                   static_cast<long long>(c.target - c.accepted),
                   c.p50_us / 1000.0, c.p99_us / 1000.0, c.max_us / 1000.0);
            const std::string tag = "tcp_accept_" + std::to_string(th) + "threads";
            AddResult(results, tag, c.rate, "conn/s",
                      "threads=" + std::to_string(th) +
                      " per_thread=" + std::to_string(per) +
                      " clients_ok=" + std::to_string(c.clients_ok) +
                      " accepted=" + std::to_string(c.accepted) +
                      " target=" + std::to_string(c.target) +
                      " lost=" + std::to_string(c.target - c.accepted));
            AddResult(results, tag + "_thread_p50_ms", c.p50_us / 1000.0, "ms", "");
            AddResult(results, tag + "_thread_p99_ms", c.p99_us / 1000.0, "ms", "");
            AddResult(results, tag + "_thread_max_ms", c.max_us / 1000.0, "ms", "");
        }
    }

    // ── 3. Concurrent connection hold ───────────────────────────────────────
    if (run_conn) {
        for (int want : hold_list) {
            ConnResult c = BenchConn(server, port, want, hold_secs);
            printf("[conn]  req=%-7d opened=%-7lld accepted=%-7lld alive=%-7lld "
                   "fds=%-7lld rss=%lldkB peak=%lldkB%s\n",
                   want,
                   static_cast<long long>(c.opened),
                   static_cast<long long>(c.accepted),
                   static_cast<long long>(c.alive_end),
                   static_cast<long long>(c.fd_count),
                   static_cast<long long>(c.rss_end_kb),
                   static_cast<long long>(c.rss_peak_kb),
                   c.aborted ? "  ABORTED(connect failed)" : "");
            const std::string tag = "conn_" + std::to_string(want);
            AddResult(results, tag + "_opened", static_cast<double>(c.opened),
                      "conns", "requested=" + std::to_string(c.requested) +
                      " aborted=" + (c.aborted ? "1" : "0"));
            AddResult(results, tag + "_accepted", static_cast<double>(c.accepted),
                      "conns", "");
            AddResult(results, tag + "_alive_end", static_cast<double>(c.alive_end),
                      "conns", "hold_sec=" + std::to_string(hold_secs));
            AddResult(results, tag + "_fd_count", static_cast<double>(c.fd_count),
                      "fds", "");
            AddResult(results, tag + "_rss_kb", static_cast<double>(c.rss_end_kb),
                      "kB", "peak=" + std::to_string(c.rss_peak_kb));
            if (c.opened > 0) {
                AddResult(results, tag + "_rss_per_conn_kb",
                          static_cast<double>(c.rss_end_kb - c.rss_start_kb) /
                              static_cast<double>(c.opened),
                          "kB/conn", "");
            }
        }
    }

    // ── 4. Server-send throughput ───────────────────────────────────────────
    if (run_bw) {
        BwResult b = BenchBw(loop, server, port, bw_conns, bw_payload,
                             bw_duration, bw_tick_ms, bw_drainers);
        printf("[bw]   conns=%lld payload=%lld tick=%lldms elapsed=%.1fs  "
               "rx=%.1f MB/s  tx=%.1f MB/s  undelivered=%lld B  rss=%lldkB\n",
               static_cast<long long>(b.conns),
               static_cast<long long>(b.payload),
               static_cast<long long>(b.tick_ms), b.elapsed_s,
               b.rx_mbps, b.tx_mbps,
               static_cast<long long>(b.undelivered_bytes),
               static_cast<long long>(b.rss_end_kb));
        AddResult(results, "bw_rx_mbps", b.rx_mbps, "MB/s",
                  "conns=" + std::to_string(b.conns) +
                  " payload=" + std::to_string(b.payload) +
                  " tick_ms=" + std::to_string(b.tick_ms) +
                  " verified_by=client");
        AddResult(results, "bw_tx_mbps", b.tx_mbps, "MB/s",
                  "note=handed_to_Send_upper_bound");
        AddResult(results, "bw_rx_bytes_per_conn", b.conns > 0
                      ? static_cast<double>(b.rx_bytes) /
                            static_cast<double>(b.conns) / b.elapsed_s / 1e6
                      : 0.0,
                  "MB/s", "per connection");
        AddResult(results, "bw_undelivered_bytes",
                  static_cast<double>(b.undelivered_bytes), "B",
                  "tx_minus_rx_after_drain");
        AddResult(results, "bw_rss_kb", static_cast<double>(b.rss_end_kb), "kB",
                  "peak=" + std::to_string(b.rss_peak_kb));
    }

    // ── 5. EventLoop dispatch latency ───────────────────────────────────────
    if (run_loop) {
        const int64_t loops = loop.GetLoopCount(0);
        const double  avg   = loop.GetAvgLoopUs(0);
        const int     fds   = loop.GetActiveFdCount(0);
        printf("[loop] count=%lld avg=%.1f us active_fd=%d\n",
               static_cast<long long>(loops), avg, fds);
        AddResult(results, "eventloop_avg_us", avg, "us",
                  "loop_count=" + std::to_string(loops));
        AddResult(results, "eventloop_active_fd", fds, "fds",
                  "sampled_after_benchmarks");
    }

    // ── Teardown ────────────────────────────────────────────────────────────
    server.Stop();
    loop.Quit();

    const int64_t peak = ProcStatusKb("VmHWM:");

    // ── Report ──────────────────────────────────────────────────────────────
    printf("\n%-34s %14s  %-8s %s\n", "metric", "value", "unit", "note");
    printf("%-34s %14s  %-8s %s\n", "----------------------------------",
           "--------------", "--------", "----");
    for (const auto& r : results) {
        char v[32];
        std::snprintf(v, sizeof(v), r.value >= 1000 ? "%.0f" : "%.2f", r.value);
        printf("%-34s %14s  %-8s %s\n", r.name.c_str(), v, r.unit.c_str(),
               r.note.c_str());
    }
    printf("\npeak RSS: %lld kB (start %lld kB)\n",
           static_cast<long long>(peak), static_cast<long long>(rss_start));

    if (!json_out.empty()) {
        std::ofstream f(json_out);
        f << "{\n";
        f << "  \"build\": \"" << STREAMSIGHT_BUILD_TYPE << "\",\n";
        f << "  \"bench\": \"" << bench << "\",\n";
        f << "  \"loop_threads\": " << loop_threads << ",\n";
        f << "  \"connects\": " << connects << ",\n";
        f << "  \"ring_ops\": " << ring_ops << ",\n";
        f << "  \"hold_secs\": " << hold_secs << ",\n";
        f << "  \"peak_rss_kb\": " << peak << ",\n";
        f << "  \"metrics\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            f << "    {\"name\": \"" << r.name << "\", \"value\": " << r.value
              << ", \"unit\": \"" << r.unit << "\", \"note\": \"" << r.note
              << "\"}" << (i + 1 < results.size() ? "," : "") << "\n";
        }
        f << "  ]\n}\n";
    }
    return 0;
}
