#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "rtsp/GopCache.h"
#include "rtsp/media.h"
#include "rtsp/rtp.h"

using streamsight::rtsp::AUDIO_FRAME;
using streamsight::rtsp::CachedPacket;
using streamsight::rtsp::channel_0;
using streamsight::rtsp::GopCache;
using streamsight::rtsp::H264;
using streamsight::rtsp::H265;
using streamsight::rtsp::MediaChannelId;
using streamsight::rtsp::MediaType;
using streamsight::rtsp::RtpPacket;
using streamsight::rtsp::VIDEO_FRAME_I;
using streamsight::rtsp::VIDEO_FRAME_P;

namespace {

RtpPacket MakePacket(uint8_t type, const std::vector<uint8_t>& payload, bool last,
                     uint32_t timestamp) {
  RtpPacket pkt;
  pkt.type = type;
  pkt.last = last ? 1 : 0;
  pkt.timestamp = timestamp;
  pkt.size = 16 + static_cast<uint32_t>(payload.size());
  std::memcpy(pkt.data.get() + 16, payload.data(), payload.size());
  return pkt;
}

std::vector<uint8_t> AnnexB(uint8_t nal_header, size_t extra = 8) {
  std::vector<uint8_t> out(4 + 1 + extra, 0x5A);
  out[0] = 0;
  out[1] = 0;
  out[2] = 0;
  out[3] = 1;
  out[4] = nal_header;
  return out;
}

void StoreNal(GopCache& cache, MediaType type, uint8_t header, uint8_t frame_type,
              bool last, uint32_t ts, size_t extra = 8) {
  cache.Store(channel_0, type,
              MakePacket(frame_type, AnnexB(header, extra), last, ts));
}

std::vector<uint8_t> NalHeaders(const std::vector<CachedPacket>& packets) {
  std::vector<uint8_t> out;
  for (const auto& item : packets) {
    const uint8_t* payload = item.pkt.data.get() + 16;
    if (item.pkt.size >= 20 && payload[0] == 0 && payload[1] == 0 &&
        payload[2] == 0 && payload[3] == 1) {
      out.push_back(payload[4]);
    } else {
      out.push_back(0);
    }
  }
  return out;
}

}  // namespace

TEST(GopCache, KeepsParamSetsAcrossKeyframes) {
  GopCache cache;
  StoreNal(cache, H264, 0x67, VIDEO_FRAME_I, true, 1000);
  StoreNal(cache, H264, 0x68, VIDEO_FRAME_I, true, 2000);
  StoreNal(cache, H264, 0x65, VIDEO_FRAME_I, true, 3000);
  StoreNal(cache, H264, 0x41, VIDEO_FRAME_P, true, 4000);

  StoreNal(cache, H264, 0x65, VIDEO_FRAME_I, true, 5000);
  StoreNal(cache, H264, 0x41, VIDEO_FRAME_P, true, 6000);

  const auto snapshot = cache.Snapshot();
  EXPECT_EQ(NalHeaders(snapshot), (std::vector<uint8_t>{0x67, 0x68, 0x65, 0x41}));
  EXPECT_EQ(snapshot[2].pkt.timestamp, 5000u);
  EXPECT_EQ(snapshot[3].pkt.timestamp, 6000u);
}

TEST(GopCache, NewParamSetClearsGop) {
  GopCache cache;
  StoreNal(cache, H264, 0x67, VIDEO_FRAME_I, true, 1000);
  StoreNal(cache, H264, 0x68, VIDEO_FRAME_I, true, 2000);
  StoreNal(cache, H264, 0x65, VIDEO_FRAME_I, true, 3000);
  StoreNal(cache, H264, 0x41, VIDEO_FRAME_P, true, 4000);
  ASSERT_FALSE(cache.Empty());

  StoreNal(cache, H264, 0x67, VIDEO_FRAME_I, true, 5000);
  EXPECT_EQ(NalHeaders(cache.Snapshot()), (std::vector<uint8_t>{0x67}));
  EXPECT_EQ(cache.FrameCount(), 1u);
}

TEST(GopCache, IgnoresAudioBeforeVideoAndAppendsAfter) {
  GopCache cache;
  cache.Store(channel_0, H264,
              MakePacket(AUDIO_FRAME, std::vector<uint8_t>(8, 0), true, 1000));
  EXPECT_TRUE(cache.Empty());

  StoreNal(cache, H264, 0x65, VIDEO_FRAME_I, true, 2000);
  cache.Store(channel_0, H264,
              MakePacket(AUDIO_FRAME, std::vector<uint8_t>(8, 0), true, 3000));
  const auto snapshot = cache.Snapshot();
  ASSERT_EQ(snapshot.size(), 2u);
  EXPECT_EQ(snapshot[0].pkt.type, VIDEO_FRAME_I);
  EXPECT_EQ(snapshot[1].pkt.type, AUDIO_FRAME);
}

TEST(GopCache, ClearsAfterPureAudioOverflow) {
  GopCache cache;
  StoreNal(cache, H264, 0x65, VIDEO_FRAME_I, true, 1000);
  for (int i = 0; i < 115; ++i) {
    cache.Store(channel_0, H264,
                MakePacket(AUDIO_FRAME, std::vector<uint8_t>(8, 0), true,
                           2000u + i));
  }
  ASSERT_FALSE(cache.Empty());
  cache.Store(channel_0, H264,
              MakePacket(AUDIO_FRAME, std::vector<uint8_t>(8, 0), true, 9999));
  EXPECT_TRUE(cache.Empty());
}

TEST(GopCache, ClearsWhenMaxFramesExceeded) {
  GopCache cache;
  cache.SetMaxFrames(3);
  StoreNal(cache, H264, 0x65, VIDEO_FRAME_I, true, 1000);
  StoreNal(cache, H264, 0x41, VIDEO_FRAME_P, true, 2000);
  StoreNal(cache, H264, 0x41, VIDEO_FRAME_P, true, 3000);
  EXPECT_EQ(cache.FrameCount(), 3u);
  StoreNal(cache, H264, 0x41, VIDEO_FRAME_P, true, 4000);
  EXPECT_TRUE(cache.Empty());
}

TEST(GopCache, ClearsWhenMaxBytesExceeded) {
  GopCache cache;
  cache.SetMaxBytes(100);
  StoreNal(cache, H264, 0x65, VIDEO_FRAME_I, true, 1000, 48);
  StoreNal(cache, H264, 0x41, VIDEO_FRAME_P, true, 2000, 48);
  EXPECT_TRUE(cache.Empty());
}

TEST(GopCache, AssemblesFragmentedIdr) {
  GopCache cache;
  cache.Store(channel_0, H264,
              MakePacket(VIDEO_FRAME_I, AnnexB(0x65, 4), false, 1000));
  EXPECT_TRUE(cache.Empty());
  cache.Store(channel_0, H264,
              MakePacket(VIDEO_FRAME_I, AnnexB(0x65, 4), true, 1000));
  ASSERT_EQ(cache.FrameCount(), 2u);
  StoreNal(cache, H264, 0x41, VIDEO_FRAME_P, true, 2000);
  EXPECT_EQ(cache.FrameCount(), 3u);
}

TEST(GopCache, DiscardsIncompleteIdrWhenInterrupted) {
  GopCache cache;
  cache.Store(channel_0, H264,
              MakePacket(VIDEO_FRAME_I, AnnexB(0x65, 4), false, 1000));
  StoreNal(cache, H264, 0x41, VIDEO_FRAME_P, true, 2000);
  EXPECT_TRUE(cache.Empty());
}

TEST(GopCache, SnapshotDeepCopies) {
  GopCache cache;
  auto packet = MakePacket(VIDEO_FRAME_I, AnnexB(0x65), true, 1000);
  cache.Store(channel_0, H264, packet);
  auto snapshot = cache.Snapshot();
  ASSERT_EQ(snapshot.size(), 1u);
  snapshot[0].pkt.data.get()[16] = 0xFF;
  const auto again = cache.Snapshot();
  EXPECT_EQ(again[0].pkt.data.get()[16], 0);
}

TEST(GopCache, DisabledCacheStoresNothing) {
  GopCache cache;
  cache.SetEnabled(false);
  StoreNal(cache, H264, 0x65, VIDEO_FRAME_I, true, 1000);
  EXPECT_TRUE(cache.Empty());
}

TEST(GopCache, IgnoresUnsupportedVideoCodec) {
  GopCache cache;
  StoreNal(cache, streamsight::rtsp::NONE, 0x65, VIDEO_FRAME_I, true, 1000);
  EXPECT_TRUE(cache.Empty());
}

TEST(GopCache, OrdersH265ParamSets) {
  GopCache cache;
  StoreNal(cache, H265, 0x40, VIDEO_FRAME_I, true, 1000);  // VPS type 32
  StoreNal(cache, H265, 0x42, VIDEO_FRAME_I, true, 2000);  // SPS type 33
  StoreNal(cache, H265, 0x44, VIDEO_FRAME_I, true, 3000);  // PPS type 34
  StoreNal(cache, H265, 0x26, VIDEO_FRAME_I, true, 4000);  // IDR type 19
  StoreNal(cache, H265, 0x02, VIDEO_FRAME_P, true, 5000);
  const auto headers = NalHeaders(cache.Snapshot());
  EXPECT_EQ(headers.size(), 5u);
  EXPECT_EQ(headers[0], 0x40);
  EXPECT_EQ(headers[1], 0x42);
  EXPECT_EQ(headers[2], 0x44);
  EXPECT_EQ(headers[3], 0x26);
  EXPECT_EQ(headers[4], 0x02);
}
