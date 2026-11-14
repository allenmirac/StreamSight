#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace streamsight::test {

struct RtpPacketView {
  uint8_t channel = 0;
  std::vector<uint8_t> rtp;
  uint8_t payload_type = 0;
  bool marker = false;
  uint16_t seq = 0;
  uint32_t timestamp = 0;
  uint32_t ssrc = 0;

  const uint8_t* payload() const { return rtp.data() + 12; }
  size_t payload_size() const {
    return rtp.size() >= 12 ? rtp.size() - 12 : 0;
  }
};

class RtspTestClient {
 public:
  explicit RtspTestClient(uint16_t port) : port_(port) {}
  ~RtspTestClient() { Close(); }

  bool Connect() {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      Close();
      return false;
    }
    return true;
  }

  void Close() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
    CloseUdpSockets();
  }

  uint32_t session_id() const { return session_id_; }
  std::string BaseUrl() const {
    return "rtsp://127.0.0.1:" + std::to_string(port_);
  }

  bool Options(std::string* response, const std::string& path = "/live") {
    return Request("OPTIONS", path, "", response);
  }

  bool Describe(const std::string& path, std::string* sdp,
                std::string* response) {
    if (!Request("DESCRIBE", path, "Accept: application/sdp\r\n", response)) {
      return false;
    }
    if (sdp) *sdp = body_;
    return true;
  }

  bool SetupTcp(const std::string& path, int rtp_channel, int rtcp_channel,
                std::string* response) {
    char transport[128];
    std::snprintf(transport, sizeof(transport),
                  "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n",
                  rtp_channel, rtcp_channel);
    return Request("SETUP", path, transport, response);
  }

  // SETUP over UDP. Both client sockets are bound with port 0 up front and
  // kept open, so the server always has a valid peer address and there is no
  // free-port race. The server's ports are parsed from the response.
  bool SetupUdp(const std::string& path, std::string* response) {
    if (!OpenUdpSockets()) return false;
    char transport[160];
    std::snprintf(transport, sizeof(transport),
                  "Transport: RTP/AVP;unicast;client_port=%u-%u\r\n",
                  static_cast<unsigned>(client_rtp_port_),
                  static_cast<unsigned>(client_rtcp_port_));
    if (!Request("SETUP", path, transport, response)) return false;
    return ParseServerPort(*response, &server_rtp_port_, &server_rtcp_port_);
  }

  uint16_t client_rtp_port() const { return client_rtp_port_; }
  uint16_t client_rtcp_port() const { return client_rtcp_port_; }
  uint16_t server_rtp_port() const { return server_rtp_port_; }
  uint16_t server_rtcp_port() const { return server_rtcp_port_; }

  bool Play(std::string* response, const std::string& path = "/live") {
    return Request("PLAY", path, SessionHeader(), response);
  }

  bool Teardown(std::string* response, const std::string& path = "/live") {
    return Request("TEARDOWN", path, SessionHeader(), response);
  }

  bool GetParameter(std::string* response, const std::string& path = "/live") {
    return Request("GET_PARAMETER", path, SessionHeader(), response);
  }

  bool ReadRtp(RtpPacketView* out, int timeout_ms = 2000) {
    if (!pending_rtp_.empty()) {
      *out = std::move(pending_rtp_.front());
      pending_rtp_.pop_front();
      return true;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    for (;;) {
      if (ExtractRtp(out)) return true;
      const int remain = RemainingMs(deadline);
      if (remain <= 0 || !Fill(remain)) return false;
    }
  }

  // Receive one RTP packet on the client UDP RTP socket.
  bool ReadUdpRtp(RtpPacketView* out, int timeout_ms = 2000) {
    if (udp_rtp_fd_ < 0) return false;
    pollfd pfd{udp_rtp_fd_, POLLIN, 0};
    if (::poll(&pfd, 1, timeout_ms) <= 0) return false;
    uint8_t buf[2048];
    const ssize_t n = ::recv(udp_rtp_fd_, buf, sizeof(buf), 0);
    if (n < 12) return false;
    RtpPacketView view;
    view.rtp.assign(buf, buf + n);
    if (!ParseRtpView(&view)) return false;
    *out = std::move(view);
    return true;
  }

  // Send an RTCP compound packet to the server's RTCP port from the client
  // RTCP socket, matching how a real client pairs its ports.
  bool SendUdpRtcp(const uint8_t* data, size_t size) {
    if (udp_rtcp_fd_ < 0 || server_rtcp_port_ == 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_rtcp_port_);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return ::sendto(udp_rtcp_fd_, data, size, 0,
                    reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
           static_cast<ssize_t>(size);
  }

  bool SendInterleaved(uint8_t channel, const uint8_t* data, size_t size) {
    std::vector<uint8_t> frame(4 + size);
    frame[0] = '$';
    frame[1] = channel;
    frame[2] = static_cast<uint8_t>((size >> 8) & 0xFF);
    frame[3] = static_cast<uint8_t>(size & 0xFF);
    std::memcpy(frame.data() + 4, data, size);
    return SendRaw(frame.data(), frame.size());
  }

 private:
  static bool BindUdp(int fd, uint16_t* port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;  // let the OS pick a free port
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      return false;
    }
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
      return false;
    }
    *port = ntohs(addr.sin_port);
    return *port != 0;
  }

  bool OpenUdpSockets() {
    if (udp_rtp_fd_ >= 0 && udp_rtcp_fd_ >= 0) return true;
    udp_rtp_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    udp_rtcp_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_rtp_fd_ < 0 || udp_rtcp_fd_ < 0 ||
        !BindUdp(udp_rtp_fd_, &client_rtp_port_) ||
        !BindUdp(udp_rtcp_fd_, &client_rtcp_port_)) {
      CloseUdpSockets();
      return false;
    }
    return true;
  }

  void CloseUdpSockets() {
    if (udp_rtp_fd_ >= 0) {
      ::close(udp_rtp_fd_);
      udp_rtp_fd_ = -1;
    }
    if (udp_rtcp_fd_ >= 0) {
      ::close(udp_rtcp_fd_);
      udp_rtcp_fd_ = -1;
    }
    client_rtp_port_ = 0;
    client_rtcp_port_ = 0;
  }

  static bool ParseServerPort(const std::string& response, uint16_t* rtp,
                              uint16_t* rtcp) {
    const size_t pos = response.find("server_port=");
    if (pos == std::string::npos) return false;
    unsigned r = 0;
    unsigned c = 0;
    if (std::sscanf(response.c_str() + pos, "server_port=%u-%u", &r, &c) !=
        2) {
      return false;
    }
    if (r == 0 || r > 65535 || c == 0 || c > 65535) return false;
    *rtp = static_cast<uint16_t>(r);
    *rtcp = static_cast<uint16_t>(c);
    return true;
  }

  std::string SessionHeader() const {
    return "Session: " + std::to_string(session_id_) + "\r\n";
  }

  bool Request(const std::string& method, const std::string& path,
               const std::string& extra, std::string* response) {
    std::string req = method + " " + BaseUrl() + path + " RTSP/1.0\r\nCSeq: " +
                      std::to_string(cseq_++) + "\r\n" + extra + "\r\n";
    if (!SendRaw(reinterpret_cast<const uint8_t*>(req.data()), req.size())) {
      return false;
    }
    return ReadResponse(response);
  }

  bool SendRaw(const uint8_t* data, size_t size) {
    size_t off = 0;
    while (off < size) {
      const ssize_t n = ::send(fd_, data + off, size - off, 0);
      if (n <= 0) return false;
      off += static_cast<size_t>(n);
    }
    return true;
  }

  bool Fill(int timeout_ms) {
    pollfd pfd{fd_, POLLIN, 0};
    const int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret <= 0) return false;
    uint8_t buf[4096];
    const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
    if (n <= 0) return false;
    in_.insert(in_.end(), buf, buf + n);
    return true;
  }

  bool ReadResponse(std::string* response) {
    for (;;) {
      if (ExtractResponse(response)) return true;
      if (!Fill(2000)) return false;
    }
  }

  static bool ParseRtpView(RtpPacketView* view) {
    if (view->rtp.size() < 12) return false;
    view->payload_type = view->rtp[1] & 0x7F;
    view->marker = (view->rtp[1] & 0x80) != 0;
    view->seq = static_cast<uint16_t>((view->rtp[2] << 8) | view->rtp[3]);
    view->timestamp = (static_cast<uint32_t>(view->rtp[4]) << 24) |
                      (static_cast<uint32_t>(view->rtp[5]) << 16) |
                      (static_cast<uint32_t>(view->rtp[6]) << 8) | view->rtp[7];
    view->ssrc = (static_cast<uint32_t>(view->rtp[8]) << 24) |
                 (static_cast<uint32_t>(view->rtp[9]) << 16) |
                 (static_cast<uint32_t>(view->rtp[10]) << 8) | view->rtp[11];
    return true;
  }

  bool ExtractRtp(RtpPacketView* out) {
    if (consumed_ + 4 > in_.size() || in_[consumed_] != '$') return false;
    const size_t len = (static_cast<size_t>(in_[consumed_ + 2]) << 8) |
                       in_[consumed_ + 3];
    if (consumed_ + 4 + len > in_.size()) return false;
    RtpPacketView view;
    view.channel = in_[consumed_ + 1];
    view.rtp.assign(in_.begin() + consumed_ + 4,
                    in_.begin() + consumed_ + 4 + len);
    if (!ParseRtpView(&view)) return false;
    *out = std::move(view);
    consumed_ += 4 + len;
    Compact();
    return true;
  }

  bool ExtractResponse(std::string* response) {
    for (;;) {
      RtpPacketView skipped;
      if (consumed_ + 4 <= in_.size() && in_[consumed_] == '$') {
        if (consumed_ + 4 > in_.size()) return false;
        const size_t len = (static_cast<size_t>(in_[consumed_ + 2]) << 8) |
                           in_[consumed_ + 3];
        if (consumed_ + 4 + len > in_.size()) return false;
        skipped.channel = in_[consumed_ + 1];
        skipped.rtp.assign(in_.begin() + consumed_ + 4,
                           in_.begin() + consumed_ + 4 + len);
        if (!ParseRtpView(&skipped)) return false;
        pending_rtp_.push_back(std::move(skipped));
        consumed_ += 4 + len;
        continue;
      }
      break;
    }

    static const char kSep[] = "\r\n\r\n";
    auto begin = in_.begin() + static_cast<std::ptrdiff_t>(consumed_);
    auto it = std::search(begin, in_.end(), kSep, kSep + 4);
    if (it == in_.end()) return false;
    const size_t header_end =
        static_cast<size_t>(std::distance(in_.begin(), it)) + 4;
    std::string header(in_.begin() + static_cast<std::ptrdiff_t>(consumed_),
                       in_.begin() + static_cast<std::ptrdiff_t>(header_end));
    std::string lower = header;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    size_t content_length = 0;
    const size_t cl = lower.find("content-length:");
    if (cl != std::string::npos) {
      content_length =
          std::strtoul(lower.c_str() + cl + 15, nullptr, 10);
    }
    const size_t total = header_end + content_length;
    if (total > in_.size()) return false;
    body_.assign(in_.begin() + static_cast<std::ptrdiff_t>(header_end),
                 in_.begin() + static_cast<std::ptrdiff_t>(total));
    ParseSession(header);
    if (response) *response = header + body_;
    consumed_ = total;
    Compact();
    return true;
  }

  void ParseSession(const std::string& header) {
    const size_t pos = header.find("Session:");
    if (pos == std::string::npos) return;
    session_id_ = static_cast<uint32_t>(
        std::strtoul(header.c_str() + pos + 8, nullptr, 10));
  }

  static int RemainingMs(
      const std::chrono::steady_clock::time_point& deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    return left.count() > 0 ? static_cast<int>(left.count()) : 0;
  }

  void Compact() {
    if (consumed_ == 0) return;
    in_.erase(in_.begin(),
              in_.begin() + static_cast<std::ptrdiff_t>(consumed_));
    consumed_ = 0;
  }

  uint16_t port_;
  int fd_ = -1;
  int udp_rtp_fd_ = -1;
  int udp_rtcp_fd_ = -1;
  uint16_t client_rtp_port_ = 0;
  uint16_t client_rtcp_port_ = 0;
  uint16_t server_rtp_port_ = 0;
  uint16_t server_rtcp_port_ = 0;
  uint32_t cseq_ = 1;
  uint32_t session_id_ = 0;
  std::vector<uint8_t> in_;
  size_t consumed_ = 0;
  std::string body_;
  std::deque<RtpPacketView> pending_rtp_;
};

}  // namespace streamsight::test
