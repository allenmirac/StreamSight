#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "net/EventLoop.h"
#include "rtsp/H264Source.h"
#include "rtsp/MediaSession.h"
#include "rtsp/RtspServer.h"
#include "rtsp/media.h"
#include "support/free_port.h"
#include "support/rtsp_test_client.h"

using streamsight::net::EventLoop;
using streamsight::rtsp::MediaSession;
using streamsight::rtsp::RtspServer;
using streamsight::test::FreeTcpPort;
using streamsight::test::RtpPacketView;
using streamsight::test::RtspTestClient;

namespace {

template <typename Predicate>
bool WaitFor(Predicate predicate,
             std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

class RtspServerFixture {
 public:
  RtspServerFixture()
      : loop_(1), server_(RtspServer::Create(&loop_)) {}

  ~RtspServerFixture() {
    if (server_) server_->Stop();
  }

  bool Start() {
    port_ = FreeTcpPort();
    if (port_ == 0) return false;
    if (!server_->Start("127.0.0.1", port_)) return false;
    session_ = MediaSession::CreateNew("live");
    session_->AddSource(streamsight::rtsp::channel_0,
                        streamsight::rtsp::H264Source::CreateNew(25));
    session_id_ = server_->AddSession(session_);
    return session_id_ != 0;
  }

  uint16_t port() const { return port_; }
  MediaSession* session() const { return session_; }
  uint32_t session_id() const { return session_id_; }
  bool PushH264(const std::vector<uint8_t>& nal, uint8_t type, uint32_t ts) {
    streamsight::rtsp::AVFrame frame(4 + nal.size());
    const uint8_t start[4] = {0, 0, 0, 1};
    std::memcpy(frame.buffer.get(), start, 4);
    std::memcpy(frame.buffer.get() + 4, nal.data(), nal.size());
    frame.size = 4 + nal.size();
    frame.type = type;
    frame.timestamp = ts;
    return server_->PushFrame(session_id_, streamsight::rtsp::channel_0,
                              frame);
  }

 private:
  EventLoop loop_;
  std::shared_ptr<RtspServer> server_;
  MediaSession* session_ = nullptr;
  uint16_t port_ = 0;
  uint32_t session_id_ = 0;
};

bool Handshake(RtspTestClient* client,
               std::string* describe_response = nullptr) {
  if (!client->Connect()) return false;
  std::string resp, sdp;
  if (!client->Options(&resp)) return false;
  if (resp.find("RTSP/1.0 200 OK") == std::string::npos) return false;
  if (!client->Describe("/live", &sdp, &resp)) return false;
  if (describe_response) *describe_response = resp;
  if (sdp.find("m=video") == std::string::npos ||
      sdp.find("a=control:track0") == std::string::npos) {
    return false;
  }
  if (!client->SetupTcp("/live/track0", 0, 1, &resp)) return false;
  if (!client->Play(&resp)) return false;
  return resp.find("RTSP/1.0 200 OK") != std::string::npos;
}

std::vector<uint8_t> Nal(uint8_t header, size_t payload_size) {
  std::vector<uint8_t> out(1 + payload_size, 0x5A);
  out[0] = header;
  return out;
}

}  // namespace

TEST(RtspIntegration, HandshakeAndStateMachine) {
  RtspServerFixture fixture;
  ASSERT_TRUE(fixture.Start());

  RtspTestClient client(fixture.port());
  ASSERT_TRUE(client.Connect());

  std::string resp;
  ASSERT_TRUE(client.Options(&resp));
  EXPECT_NE(resp.find("RTSP/1.0 200 OK"), std::string::npos);
  EXPECT_NE(resp.find("Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY"),
            std::string::npos);

  ASSERT_TRUE(client.Describe("/missing", nullptr, &resp));
  EXPECT_NE(resp.find("404 Stream Not Found"), std::string::npos);

  std::string sdp;
  ASSERT_TRUE(client.Describe("/live", &sdp, &resp));
  EXPECT_NE(sdp.find("m=video 0 RTP/AVP 96"), std::string::npos);
  EXPECT_NE(sdp.find("a=rtpmap:96 H264/90000"), std::string::npos);

  ASSERT_TRUE(client.SetupTcp("/live/track0", 0, 1, &resp));
  EXPECT_NE(resp.find("Transport: RTP/AVP/TCP;unicast;interleaved=0-1"),
            std::string::npos);
  EXPECT_NE(resp.find("Session:"), std::string::npos);

  ASSERT_TRUE(client.Play(&resp));
  EXPECT_NE(resp.find("RTSP/1.0 200 OK"), std::string::npos);
  EXPECT_NE(resp.find("timeout=60"), std::string::npos);

  ASSERT_TRUE(client.GetParameter(&resp));
  EXPECT_NE(resp.find("RTSP/1.0 200 OK"), std::string::npos);

  ASSERT_TRUE(client.Teardown(&resp));
  EXPECT_NE(resp.find("RTSP/1.0 200 OK"), std::string::npos);

  client.Close();
  EXPECT_TRUE(WaitFor([&]() { return fixture.session()->GetNumClient() == 0; }));
}

TEST(RtspIntegration, SetupBeforeDescribeReturnsServerError) {
  RtspServerFixture fixture;
  ASSERT_TRUE(fixture.Start());

  RtspTestClient client(fixture.port());
  ASSERT_TRUE(client.Connect());
  std::string resp;
  ASSERT_TRUE(client.SetupTcp("/live/track0", 0, 1, &resp));
  EXPECT_NE(resp.find("500 Internal Server Error"), std::string::npos);
}
