#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "net/EventLoop.h"
#include "observe/MetricsRegistry.h"
#include "rtsp/H264Source.h"
#include "rtsp/MediaSession.h"
#include "rtsp/RtcpMessage.h"
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

[[maybe_unused]] bool Handshake(RtspTestClient* client,
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

[[maybe_unused]] std::vector<uint8_t> Nal(uint8_t header,
                                          size_t payload_size) {
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

namespace {

// Build a 32-byte RTCP RR with a single report block (RFC 3550 §6.4.2).
std::vector<uint8_t> BuildRr(uint32_t media_ssrc, uint8_t fraction,
                             int32_t lost, uint32_t jitter) {
  std::vector<uint8_t> rr(32, 0);
  rr[0] = 0x81;  // V=2, RC=1
  rr[1] = 201;
  rr[2] = 0;
  rr[3] = 7;  // 32 bytes
  const uint32_t now =
      static_cast<uint32_t>(streamsight::rtsp::GetNtpTimestamp() >> 16);
  const uint32_t lsr = now - 0x10000u;  // one second ago
  const uint32_t dlsr = 0x8000u;        // 0.5 second
  rr[4] = 1;
  rr[5] = 2;
  rr[6] = 3;
  rr[7] = 4;
  const size_t b = 8;
  rr[b + 0] = static_cast<uint8_t>(media_ssrc >> 24);
  rr[b + 1] = static_cast<uint8_t>(media_ssrc >> 16);
  rr[b + 2] = static_cast<uint8_t>(media_ssrc >> 8);
  rr[b + 3] = static_cast<uint8_t>(media_ssrc);
  rr[b + 4] = fraction;
  const uint32_t lost24 = static_cast<uint32_t>(lost) & 0xFFFFFFu;
  rr[b + 5] = static_cast<uint8_t>(lost24 >> 16);
  rr[b + 6] = static_cast<uint8_t>(lost24 >> 8);
  rr[b + 7] = static_cast<uint8_t>(lost24);
  rr[b + 8] = 0;
  rr[b + 9] = 0;
  rr[b + 10] = 1;
  rr[b + 11] = 0x23;
  rr[b + 12] = static_cast<uint8_t>(jitter >> 24);
  rr[b + 13] = static_cast<uint8_t>(jitter >> 16);
  rr[b + 14] = static_cast<uint8_t>(jitter >> 8);
  rr[b + 15] = static_cast<uint8_t>(jitter);
  rr[b + 16] = static_cast<uint8_t>(lsr >> 24);
  rr[b + 17] = static_cast<uint8_t>(lsr >> 16);
  rr[b + 18] = static_cast<uint8_t>(lsr >> 8);
  rr[b + 19] = static_cast<uint8_t>(lsr);
  rr[b + 20] = static_cast<uint8_t>(dlsr >> 24);
  rr[b + 21] = static_cast<uint8_t>(dlsr >> 16);
  rr[b + 22] = static_cast<uint8_t>(dlsr >> 8);
  rr[b + 23] = static_cast<uint8_t>(dlsr);
  return rr;
}

}  // namespace

TEST(RtspIntegration, RtcpReceiverReportUpdatesMetricsAndKeepsAlive) {
  RtspServerFixture fixture;
  ASSERT_TRUE(fixture.Start());
  RtspTestClient client(fixture.port());
  ASSERT_TRUE(Handshake(&client));

  const auto sps = Nal(0x67, 8);
  const auto pps = Nal(0x68, 8);
  const auto idr = Nal(0x65, 64);
  ASSERT_TRUE(fixture.PushH264(sps, streamsight::rtsp::VIDEO_FRAME_I, 1000));
  ASSERT_TRUE(fixture.PushH264(pps, streamsight::rtsp::VIDEO_FRAME_I, 2000));
  ASSERT_TRUE(fixture.PushH264(idr, streamsight::rtsp::VIDEO_FRAME_I, 3000));

  RtpPacketView rtp;
  ASSERT_TRUE(client.ReadRtp(&rtp));
  ASSERT_GT(rtp.ssrc, 0u);

  const auto rr = BuildRr(rtp.ssrc, 10, 5, 100);
  ASSERT_TRUE(client.SendInterleaved(1, rr.data(), rr.size()));

  ASSERT_TRUE(WaitFor([&]() {
    const auto snapshot =
        streamsight::observe::MetricsRegistry::Instance().Snapshot("rtcp.");
    for (const auto& kv : snapshot) {
      if (kv.first.find("_fraction_lost") != std::string::npos &&
          static_cast<int>(kv.second) == 10) {
        return true;
      }
    }
    return false;
  }));

  const auto snapshot =
      streamsight::observe::MetricsRegistry::Instance().Snapshot("rtcp.");
  bool saw_jitter = false;
  bool saw_rtt = false;
  for (const auto& kv : snapshot) {
    if (kv.first.find("_jitter") != std::string::npos && kv.second == 100.0) {
      saw_jitter = true;
    }
    if (kv.first.find("_rtt_us") != std::string::npos && kv.second > 400000.0 &&
        kv.second < 800000.0) {
      saw_rtt = true;
    }
  }
  EXPECT_TRUE(saw_jitter);
  EXPECT_TRUE(saw_rtt);

  std::string resp;
  ASSERT_TRUE(client.GetParameter(&resp));
  EXPECT_NE(resp.find("RTSP/1.0 200 OK"), std::string::npos);
}

TEST(RtspIntegration, KeyframeGatesLivePackets) {
  RtspServerFixture fixture;
  ASSERT_TRUE(fixture.Start());
  RtspTestClient client(fixture.port());
  ASSERT_TRUE(Handshake(&client));

  ASSERT_TRUE(fixture.PushH264(Nal(0x41, 32), streamsight::rtsp::VIDEO_FRAME_P,
                               1000));
  RtpPacketView view;
  EXPECT_FALSE(client.ReadRtp(&view, 200));

  ASSERT_TRUE(fixture.PushH264(Nal(0x65, 64), streamsight::rtsp::VIDEO_FRAME_I,
                               2000));
  ASSERT_TRUE(client.ReadRtp(&view, 2000));
  ASSERT_GE(view.payload_size(), 5u);
  EXPECT_EQ(view.payload()[4], 0x65);
}

TEST(RtspIntegration, LateClientReceivesCachedGopAndLiveFanout) {
  RtspServerFixture fixture;
  ASSERT_TRUE(fixture.Start());

  RtspTestClient first(fixture.port());
  ASSERT_TRUE(Handshake(&first));

  ASSERT_TRUE(fixture.PushH264(Nal(0x67, 4), streamsight::rtsp::VIDEO_FRAME_I,
                               1000));
  ASSERT_TRUE(fixture.PushH264(Nal(0x68, 4), streamsight::rtsp::VIDEO_FRAME_I,
                               2000));
  ASSERT_TRUE(fixture.PushH264(Nal(0x65, 32), streamsight::rtsp::VIDEO_FRAME_I,
                               3000));
  ASSERT_TRUE(fixture.PushH264(Nal(0x41, 32), streamsight::rtsp::VIDEO_FRAME_P,
                               4000));

  for (uint8_t expected : {0x67, 0x68, 0x65, 0x41}) {
    RtpPacketView view;
    ASSERT_TRUE(first.ReadRtp(&view, 2000));
    ASSERT_GE(view.payload_size(), 5u);
    EXPECT_EQ(view.payload()[4], expected);
  }

  RtspTestClient second(fixture.port());
  ASSERT_TRUE(Handshake(&second));

  const std::vector<uint8_t> expected_headers = {0x67, 0x68, 0x65, 0x41};
  const std::vector<uint32_t> expected_ts = {1000, 2000, 3000, 4000};
  RtpPacketView second_views[4];
  for (size_t i = 0; i < expected_headers.size(); ++i) {
    ASSERT_TRUE(second.ReadRtp(&second_views[i], 2000));
    ASSERT_GE(second_views[i].payload_size(), 5u);
    EXPECT_EQ(second_views[i].payload()[4], expected_headers[i]);
    EXPECT_EQ(second_views[i].timestamp, expected_ts[i]);
  }

  ASSERT_TRUE(fixture.PushH264(Nal(0x41, 32), streamsight::rtsp::VIDEO_FRAME_P,
                               5000));
  RtpPacketView a_live;
  RtpPacketView b_live;
  ASSERT_TRUE(first.ReadRtp(&a_live, 2000));
  ASSERT_TRUE(second.ReadRtp(&b_live, 2000));
  const std::vector<uint8_t> a_payload(
      a_live.payload(), a_live.payload() + a_live.payload_size());
  const std::vector<uint8_t> b_payload(
      b_live.payload(), b_live.payload() + b_live.payload_size());
  EXPECT_EQ(a_payload, b_payload);
  EXPECT_NE(a_live.ssrc, b_live.ssrc);
}

TEST(RtspIntegration, SlowClientDoesNotBlockFastClient) {
  RtspServerFixture fixture;
  ASSERT_TRUE(fixture.Start());

  RtspTestClient fast(fixture.port());
  RtspTestClient slow(fixture.port());
  ASSERT_TRUE(Handshake(&fast));
  ASSERT_TRUE(Handshake(&slow));

  ASSERT_TRUE(fixture.PushH264(Nal(0x67, 4), streamsight::rtsp::VIDEO_FRAME_I,
                               1000));
  ASSERT_TRUE(fixture.PushH264(Nal(0x68, 4), streamsight::rtsp::VIDEO_FRAME_I,
                               2000));
  ASSERT_TRUE(fixture.PushH264(Nal(0x65, 32), streamsight::rtsp::VIDEO_FRAME_I,
                               3000));

  RtpPacketView view;
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(fast.ReadRtp(&view, 2000));
    ASSERT_TRUE(slow.ReadRtp(&view, 2000));
  }

  constexpr int kFrames = 100;
  for (int i = 0; i < kFrames; ++i) {
    ASSERT_TRUE(fixture.PushH264(Nal(0x41, 32),
                                 streamsight::rtsp::VIDEO_FRAME_P,
                                 4000u + static_cast<uint32_t>(i)));
  }

  int received = 0;
  for (int i = 0; i < kFrames; ++i) {
    if (!fast.ReadRtp(&view, 2000)) break;
    ++received;
  }
  EXPECT_EQ(received, kFrames);
}
