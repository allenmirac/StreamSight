// StreamServer.h
// Process-level RTSP media server.
//
// Owns a single shared streamsight::net::EventLoop + streamsight::rtsp::RtspServer bound to one port.
// Every stream (StreamSession) registers its own MediaSession with this
// server, so the event-loop threads are shared across all streams rather
// than duplicated per stream.
//
// Thread model: the EventLoop runs N epoll threads (N = hardware_concurrency
// by default, decoupled from the stream count). Scheduler[0] handles accept +
// timers; the remaining schedulers round-robin client connections.

#ifndef STREAMSIGHT_FFMPEG_STREAM_SERVER_H
#define STREAMSIGHT_FFMPEG_STREAM_SERVER_H

#include <memory>
#include <string>
#include <cstdint>

namespace streamsight::net {
class EventLoop;
}  // namespace streamsight::net

namespace streamsight::rtsp {
class RtspServer;
}  // namespace streamsight::rtsp

namespace streamsight::ffmpeg {

class StreamServer {
public:
    StreamServer() = default;
    ~StreamServer();

    StreamServer(const StreamServer&) = delete;
    StreamServer& operator=(const StreamServer&) = delete;

    // Bind the RTSP server on |port| and start the shared event loop.
    // |eventloop_threads| == 0 selects std::thread::hardware_concurrency().
    // Returns false on bind failure.
    bool Start(const std::string& bind_ip, uint16_t port,
               uint32_t eventloop_threads = 0);

    // Stop the RTSP server and join the event-loop threads. Idempotent.
    void Stop();

    bool IsRunning() const { return rtsp_server_ != nullptr; }

    uint16_t Port() const { return port_; }

    // Non-owning accessors for StreamSession to attach output adapters and
    // read event-loop metrics. Valid only after a successful Start().
    streamsight::rtsp::RtspServer* GetRtspServer() const { return rtsp_server_.get(); }
    streamsight::net::EventLoop*   GetEventLoop()  const { return event_loop_.get(); }

private:
    std::shared_ptr<streamsight::net::EventLoop>  event_loop_;
    std::shared_ptr<streamsight::rtsp::RtspServer> rtsp_server_;
    uint16_t port_ = 0;
};

}  // namespace streamsight::ffmpeg

#endif // STREAMSIGHT_FFMPEG_STREAM_SERVER_H