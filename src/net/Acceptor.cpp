#include "Acceptor.h"

#include "EventLoop.h"
#include "Logger.h"
#include "SocketUtil.h"

using namespace streamsight::net;

Acceptor::Acceptor(EventLoop* eventLoop)
    : event_loop_(eventLoop), tcp_socket_(new TcpSocket) {}

Acceptor::~Acceptor() {}

int Acceptor::Listen(std::string ip, uint16_t port) {
  std::lock_guard<std::mutex> locker(mutex_);

  if (tcp_socket_->GetSocket() > 0) {
    tcp_socket_->Close();
  }

  SOCKET sockfd = tcp_socket_->Create();
  channel_ptr_.reset(new Channel(sockfd));
  SocketUtil::SetReuseAddr(sockfd);
  SocketUtil::SetReusePort(sockfd);
  SocketUtil::SetNonBlock(sockfd);

  if (!tcp_socket_->Bind(ip, port)) {
    return -1;
  }

  if (!tcp_socket_->Listen(1024)) {
    return -1;
  }

  channel_ptr_->SetReadCallback([this]() { this->OnAccept(); });
  channel_ptr_->EnableReading();
  event_loop_->UpdateChannel(channel_ptr_);
  return 0;
}

void Acceptor::Close() {
  std::lock_guard<std::mutex> locker(mutex_);

  if (tcp_socket_->GetSocket() > 0) {
    event_loop_->RemoveChannel(channel_ptr_);
    tcp_socket_->Close();
  }
}

void Acceptor::OnAccept() {
  constexpr int kMaxPerIteration = 64;
  NewConnectionCallback cb;
  for (int n = 0; n < kMaxPerIteration; ++n) {
    SOCKET fd = INVALID_SOCKET;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cb = new_connection_callback_;
      fd = tcp_socket_->Accept();
		}
    if (fd < 0) {
      const int e = errno;
      if (e == EAGAIN || e == EWOULDBLOCK) break;
      if (e == EINTR || e == ECONNABORTED || e == EPROTO) continue;
      LOG_ERROR("accept failed: %s", strerror(e));
      break;
    }
    cb ? cb(fd) : SocketUtil::Close(fd);
  }
}
