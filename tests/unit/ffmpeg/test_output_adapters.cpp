// test_output_adapters.cpp
// Unit tests for the output adapter contract: MultiOutputAdapter fan-out and
// failure propagation, RtspOutputAdapter guards, and RtmpOutputAdapter
// lifecycle guards. No real network connections are attempted.

#include <gtest/gtest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
}

#include <chrono>
#include <future>
#include <memory>
#include <string>

#include "ffmpeg/IOutputAdapter.h"
#include "ffmpeg/MultiOutputAdapter.h"
#include "ffmpeg/RtmpOutputAdapter.h"
#include "ffmpeg/RtspOutputAdapter.h"
#include "net/EventLoop.h"
#include "rtsp/RtspServer.h"

using streamsight::ffmpeg::IOutputAdapter;
using streamsight::ffmpeg::MultiOutputAdapter;
using streamsight::ffmpeg::OutputConfig;
using streamsight::ffmpeg::RtmpOutputAdapter;
using streamsight::ffmpeg::RtspOutputAdapter;

namespace {

class FakeAdapter : public IOutputAdapter {
 public:
  bool open_result = true;
  bool write_result = true;
  int open_calls = 0;
  int write_calls = 0;
  int close_calls = 0;
  bool opened = false;

  bool Open(const AVCodecContext*, const OutputConfig&) override {
    ++open_calls;
    opened = open_result;
    return open_result;
  }
  void Close() override {
    ++close_calls;
    opened = false;
  }
  bool WritePacket(const AVPacket*) override {
    ++write_calls;
    return write_result;
  }
  bool IsOpened() const override { return opened; }
};

AVPacket* PacketWithSize(int size) {
  AVPacket* pkt = av_packet_alloc();
  if (size > 0) {
    av_new_packet(pkt, size);
    pkt->size = size;
    pkt->data[0] = 0x65;
  }
  return pkt;
}

// EventLoop's scheduler thread resets its shutdown flag when it starts. Wait
// until one trigger event has run so that initialization is complete before
// the test tears the loop down; otherwise an immediate Stop() can be lost and
// the destructor blocks forever joining the thread.
bool WaitForLoopStarted(streamsight::net::EventLoop* loop) {
  // Shared ownership keeps the promise alive even if the callback runs after
  // this helper times out and its stack frame is gone.
  auto started = std::make_shared<std::promise<void>>();
  auto future = started->get_future();
  if (!loop->AddTriggerEvent([started]() { started->set_value(); })) {
    return false;
  }
  return future.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
}

}  // namespace

TEST(MultiOutputAdapterTest, PropagatesCallsToAllAdapters) {
  auto first = std::make_shared<FakeAdapter>();
  auto second = std::make_shared<FakeAdapter>();
  MultiOutputAdapter multi;
  multi.AddAdapter(first);
  multi.AddAdapter(second);

  OutputConfig cfg;
  ASSERT_TRUE(multi.Open(nullptr, cfg));
  EXPECT_EQ(first->open_calls, 1);
  EXPECT_EQ(second->open_calls, 1);
  EXPECT_TRUE(multi.IsOpened());

  AVPacket* pkt = PacketWithSize(4);
  ASSERT_TRUE(multi.WritePacket(pkt));
  EXPECT_EQ(first->write_calls, 1);
  EXPECT_EQ(second->write_calls, 1);

  multi.Close();
  EXPECT_EQ(first->close_calls, 1);
  EXPECT_EQ(second->close_calls, 1);
  av_packet_free(&pkt);
}

TEST(MultiOutputAdapterTest, StopsOnFirstOpenFailure) {
  auto first = std::make_shared<FakeAdapter>();
  auto second = std::make_shared<FakeAdapter>();
  first->open_result = false;
  MultiOutputAdapter multi;
  multi.AddAdapter(first);
  multi.AddAdapter(second);

  OutputConfig cfg;
  EXPECT_FALSE(multi.Open(nullptr, cfg));
  EXPECT_EQ(first->open_calls, 1);
  EXPECT_EQ(second->open_calls, 0);
}

TEST(MultiOutputAdapterTest, StopsOnFirstWriteFailure) {
  auto first = std::make_shared<FakeAdapter>();
  auto second = std::make_shared<FakeAdapter>();
  first->write_result = false;
  MultiOutputAdapter multi;
  multi.AddAdapter(first);
  multi.AddAdapter(second);

  AVPacket* pkt = PacketWithSize(4);
  EXPECT_FALSE(multi.WritePacket(pkt));
  EXPECT_EQ(first->write_calls, 1);
  EXPECT_EQ(second->write_calls, 0);
  av_packet_free(&pkt);
}

TEST(MultiOutputAdapterTest, EmptyAdapterIsOpenedAndAcceptsWrites) {
  MultiOutputAdapter multi;
  EXPECT_EQ(multi.Count(), 0u);
  OutputConfig cfg;
  EXPECT_TRUE(multi.Open(nullptr, cfg));
  EXPECT_TRUE(multi.IsOpened());
  AVPacket* pkt = PacketWithSize(4);
  EXPECT_TRUE(multi.WritePacket(pkt));
  av_packet_free(&pkt);
}

TEST(RtspOutputAdapterTest, RejectsMissingServerOrSession) {
  OutputConfig cfg;
  RtspOutputAdapter no_server(nullptr, 1, 0);
  EXPECT_FALSE(no_server.Open(nullptr, cfg));
  EXPECT_FALSE(no_server.IsOpened());

  AVPacket* null_server_pkt = PacketWithSize(4);
  EXPECT_FALSE(no_server.WritePacket(null_server_pkt));
  av_packet_free(&null_server_pkt);

  RtspOutputAdapter no_session(nullptr, 0, 0);
  EXPECT_FALSE(no_session.Open(nullptr, cfg));
}

TEST(RtspOutputAdapterTest, RejectsWriteWhenSessionIdIsZero) {
  streamsight::net::EventLoop loop(1);
  ASSERT_TRUE(WaitForLoopStarted(&loop));
  auto server = streamsight::rtsp::RtspServer::Create(&loop);
  RtspOutputAdapter adapter(server.get(), 0, 0);
  OutputConfig cfg;
  EXPECT_FALSE(adapter.Open(nullptr, cfg));

  // A real server is present, so this exercises the session_id == 0 guard in
  // WritePacket rather than the null-server guard.
  AVPacket* pkt = PacketWithSize(4);
  EXPECT_FALSE(adapter.WritePacket(pkt));
  av_packet_free(&pkt);
  adapter.Close();
}

TEST(RtspOutputAdapterTest, SkipsEmptyPacket) {
  streamsight::net::EventLoop loop(1);
  ASSERT_TRUE(WaitForLoopStarted(&loop));
  auto server = streamsight::rtsp::RtspServer::Create(&loop);
  RtspOutputAdapter adapter(server.get(), 1, 0);
  OutputConfig cfg;
  ASSERT_TRUE(adapter.Open(nullptr, cfg));

  AVPacket* pkt = PacketWithSize(0);
  EXPECT_TRUE(adapter.WritePacket(pkt));
  av_packet_free(&pkt);
  adapter.Close();
}

TEST(RtspOutputAdapterTest, FailsWhenServerHasNoSession) {
  streamsight::net::EventLoop loop(1);
  ASSERT_TRUE(WaitForLoopStarted(&loop));
  auto server = streamsight::rtsp::RtspServer::Create(&loop);
  RtspOutputAdapter adapter(server.get(), 12345, 0);
  OutputConfig cfg;
  ASSERT_TRUE(adapter.Open(nullptr, cfg));

  AVPacket* pkt = PacketWithSize(4);
  EXPECT_FALSE(adapter.WritePacket(pkt));
  av_packet_free(&pkt);
  adapter.Close();
}

TEST(RtmpOutputAdapterTest, RejectsEmptyUrl) {
  RtmpOutputAdapter adapter("");
  OutputConfig cfg;
  EXPECT_FALSE(adapter.Open(nullptr, cfg));
  EXPECT_FALSE(adapter.IsOpened());
}

TEST(RtmpOutputAdapterTest, WriteBeforeOpenFailsAndCloseIsSafe) {
  RtmpOutputAdapter adapter("rtmp://127.0.0.1/live/test");
  AVPacket* pkt = PacketWithSize(4);
  EXPECT_FALSE(adapter.WritePacket(pkt));
  adapter.Close();
  av_packet_free(&pkt);
}
