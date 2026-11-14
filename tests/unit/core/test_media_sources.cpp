#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "rtsp/AACSource.h"
#include "rtsp/G711ASource.h"
#include "rtsp/H265Source.h"
#include "rtsp/VP8Source.h"
#include "rtsp/media.h"
#include "rtsp/rtp.h"

using namespace streamsight::rtsp;

namespace {

AVFrame MakeFrame(uint32_t size, uint8_t first_byte) {
  AVFrame frame(size);
  frame.size = size;
  frame.type = VIDEO_FRAME_I;
  frame.timestamp = 9000;
  std::memset(frame.buffer.get(), 0x5A, size);
  frame.buffer.get()[0] = first_byte;
  return frame;
}

std::vector<RtpPacket> Collect(MediaSource* source, AVFrame frame) {
  std::vector<RtpPacket> packets;
  source->SetSendFrameCallback([&](MediaChannelId, RtpPacket pkt) {
    packets.push_back(pkt);
    return true;
  });
  EXPECT_TRUE(source->HandleFrame(channel_0, frame));
  return packets;
}

std::vector<uint8_t> Bytes(const RtpPacket& pkt, size_t offset) {
  return std::vector<uint8_t>(pkt.data.get() + offset,
                              pkt.data.get() + pkt.size);
}

}  // namespace

TEST(H265SourceTest, SendsSmallNalAsSinglePacket) {
  std::unique_ptr<H265Source> source(H265Source::CreateNew(25));
  const auto packets = Collect(source.get(), MakeFrame(10, 0x26));
  ASSERT_EQ(packets.size(), 1u);
  EXPECT_EQ(packets[0].size, 10u + 16u);
  EXPECT_EQ(packets[0].last, 1u);
  EXPECT_EQ(packets[0].data.get()[16], 0x26);
}

TEST(H265SourceTest, FragmentsAndReassemblesWithFu) {
  std::unique_ptr<H265Source> source(H265Source::CreateNew(25));
  AVFrame frame = MakeFrame(3000, 0x26);
  frame.buffer.get()[1] = 0x01;
  const auto packets = Collect(source.get(), frame);
  ASSERT_EQ(packets.size(), 3u);
  EXPECT_EQ(packets[0].last, 0u);
  EXPECT_EQ(packets[1].last, 0u);
  EXPECT_EQ(packets[2].last, 1u);

  const auto first = Bytes(packets[0], 16);
  EXPECT_EQ((first[0] & 0x81) | ((first[2] & 0x3F) << 1), 0x26);
  EXPECT_NE(first[2] & 0x80, 0u);
  const auto last = Bytes(packets[2], 16);
  EXPECT_NE(last[2] & 0x40, 0u);

  std::vector<uint8_t> rebuilt;
  rebuilt.push_back(static_cast<uint8_t>((first[0] & 0x81) |
                                         ((first[2] & 0x3F) << 1)));
  rebuilt.push_back(first[1]);
  for (const auto& pkt : packets) {
    const auto bytes = Bytes(pkt, 19);
    rebuilt.insert(rebuilt.end(), bytes.begin(), bytes.end());
  }
  const std::vector<uint8_t> original(frame.buffer.get(),
                                      frame.buffer.get() + frame.size);
  EXPECT_EQ(rebuilt, original);
}

TEST(AACSourceTest, StripsAdtsAndWritesAuHeader) {
  std::unique_ptr<AACSource> source(AACSource::CreateNew(44100, 2, true));
  AVFrame frame(7 + 10);
  frame.size = 17;
  frame.type = AUDIO_FRAME;
  frame.timestamp = 1234;
  for (int i = 0; i < 17; ++i) frame.buffer.get()[i] = static_cast<uint8_t>(i);

  const auto packets = Collect(source.get(), frame);
  ASSERT_EQ(packets.size(), 1u);
  const auto& pkt = packets[0];
  EXPECT_EQ(pkt.size, 10u + 16u + 4u);
  const uint8_t* payload = pkt.data.get() + 16;
  EXPECT_EQ(payload[0], 0x00);
  EXPECT_EQ(payload[1], 0x10);
  EXPECT_EQ(payload[2], 0x00);
  EXPECT_EQ(payload[3], 0x50);  // 10 << 3
  EXPECT_EQ(payload[4], 7);     // ADTS stripped
  EXPECT_EQ(payload[13], 16);
}

TEST(AACSourceTest, RejectsOversizeFrame) {
  std::unique_ptr<AACSource> source(AACSource::CreateNew(44100, 2, false));
  int calls = 0;
  source->SetSendFrameCallback([&](MediaChannelId, RtpPacket) {
    ++calls;
    return true;
  });
  AVFrame frame(MAX_RTP_PAYLOAD_SIZE);
  frame.size = MAX_RTP_PAYLOAD_SIZE;
  frame.type = AUDIO_FRAME;
  EXPECT_FALSE(source->HandleFrame(channel_0, frame));
  EXPECT_EQ(calls, 0);
}

TEST(AACSourceTest, BuildsFmtpConfig) {
  std::unique_ptr<AACSource> source(AACSource::CreateNew(44100, 2, true));
  const std::string attr = source->GetAttribute();
  EXPECT_NE(attr.find("a=rtpmap:97 MPEG4-GENERIC/44100/2"),
            std::string::npos);
  EXPECT_NE(attr.find("config=1210"), std::string::npos);
}

TEST(G711SourceTest, SendsSinglePacketAndRejectsOversize) {
  std::unique_ptr<G711ASource> source(G711ASource::CreateNew());
  const auto packets = Collect(source.get(), MakeFrame(160, 0x00));
  ASSERT_EQ(packets.size(), 1u);
  EXPECT_EQ(packets[0].size, 160u + 16u);
  EXPECT_EQ(packets[0].last, 1u);

  AVFrame big(MAX_RTP_PAYLOAD_SIZE + 1);
  big.size = MAX_RTP_PAYLOAD_SIZE + 1;
  big.type = AUDIO_FRAME;
  EXPECT_FALSE(source->HandleFrame(channel_0, big));
}

TEST(VP8SourceTest, FragmentsWithVp8Descriptor) {
  std::unique_ptr<VP8Source> source(VP8Source::CreateNew(25));
  AVFrame frame = MakeFrame(2000, 0xAB);
  frame.size = 2000;
  frame.type = VIDEO_FRAME_I;
  const auto packets = Collect(source.get(), frame);
  ASSERT_EQ(packets.size(), 2u);
  EXPECT_EQ(packets[0].size, 4u + 12u + 1u + 1420u);
  EXPECT_EQ(packets[0].last, 0u);
  EXPECT_EQ(packets[0].data.get()[16], 0x10);
  EXPECT_EQ(packets[1].size, 4u + 12u + 1u + 580u);
  EXPECT_EQ(packets[1].last, 1u);
  EXPECT_EQ(packets[1].data.get()[16], 0x00);

  std::vector<uint8_t> rebuilt;
  for (const auto& pkt : packets) {
    const auto bytes = Bytes(pkt, 17);
    rebuilt.insert(rebuilt.end(), bytes.begin(), bytes.end());
  }
  EXPECT_EQ(rebuilt.size(), 2000u);
  EXPECT_EQ(rebuilt[0], 0xAB);
}

TEST(MediaSourcesTest, AdvertisePayloadTypesAndClockRates) {
  // Source constructors are private; the public CreateNew factories are the
  // supported construction path.
  std::unique_ptr<H265Source> h265(H265Source::CreateNew(25));
  std::unique_ptr<AACSource> aac(AACSource::CreateNew(44100, 2, true));
  std::unique_ptr<G711ASource> g711(G711ASource::CreateNew());
  std::unique_ptr<VP8Source> vp8(VP8Source::CreateNew(25));
  EXPECT_EQ(h265->GetPayloadType(), 96u);
  EXPECT_EQ(h265->GetClockRate(), 90000u);
  EXPECT_EQ(aac->GetPayloadType(), 97u);
  EXPECT_EQ(aac->GetClockRate(), 44100u);
  EXPECT_EQ(g711->GetPayloadType(), 8u);
  EXPECT_EQ(g711->GetClockRate(), 8000u);
  EXPECT_EQ(vp8->GetClockRate(), 90000u);
}
