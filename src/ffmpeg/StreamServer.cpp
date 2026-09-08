// StreamServer.cpp
// Process-level RTSP media server implementation.

#include "StreamServer.h"
#include "../xop/RtspServer.h"
#include "../net/EventLoop.h"
#include <iostream>
#include <thread>

namespace ffmpeg {

StreamServer::~StreamServer() {
    Stop();
}

bool StreamServer::Start(const std::string& bind_ip, uint16_t port,
                         uint32_t eventloop_threads) {
    if (rtsp_server_) return true;  // already started

    uint32_t threads = eventloop_threads;
    if (threads == 0) {
        unsigned hc = std::thread::hardware_concurrency();
        threads = (hc > 0) ? hc : 1;
    }

    event_loop_ = std::make_shared<xop::EventLoop>(threads);
    rtsp_server_ = xop::RtspServer::Create(event_loop_.get());
    if (!rtsp_server_->Start(bind_ip, port)) {
        std::cerr << "[StreamServer] RTSP bind failed on "
                  << bind_ip << ":" << port << std::endl;
        rtsp_server_.reset();
        event_loop_.reset();
        return false;
    }

    port_ = port;
    std::cout << "[StreamServer] RTSP listening on " << bind_ip << ":" << port
              << " (eventloop_threads=" << threads << ")" << std::endl;
    return true;
}

void StreamServer::Stop() {
    // Order matters: disconnect clients while the event loop is still running,
    // so the disconnect callbacks can drain; then join the loop threads.
    if (rtsp_server_) {
        rtsp_server_->Stop();
        rtsp_server_.reset();
    }
    if (event_loop_) {
        event_loop_->Quit();
        event_loop_.reset();
    }
    port_ = 0;
}

} // namespace ffmpeg