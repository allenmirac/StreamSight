#include <arpa/inet.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "rtsp/H264Source.h"
#include "rtsp/RtcpMessage.h"
#include "rtsp/rtp.h"

using streamsight::rtsp::AVFrame;
using streamsight::rtsp::BuildRtcpSR;
using streamsight::rtsp::channel_0;
using streamsight::rtsp::GetNtpTimestamp;
using streamsight::rtsp::H264Source;
using streamsight::rtsp::MediaChannelId;
using streamsight::rtsp::RtpPacket;
using streamsight::rtsp::RTCP_SR_SIZE;
using streamsight::rtsp::RtcpSRPacket;

namespace {

AVFrame MakeFrame(uint32_t size, uint8_t nal_header) {
  AVFrame frame(size);
  frame.size = size;
  frame.type = streamsight::rtsp::VIDEO_FRAME_I;
  frame.timestamp = 9000;
  std::memset(frame.buffer.get(), 0x5A, size);
  frame.buffer.get()[0] = nal_header;
  return frame;
}

std::vector<uint8_t> PacketBytes(const RtpPacket& packet) {
  return std::vector<uint8_t>(packet.data.get(),
                              packet.data.get() + packet.size);
}

std::vector<uint8_t> Reassemble(const std::vector<RtpPacket>& packets) {
  std::vector<uint8_t> out;
  const auto first = PacketBytes(packets.front());
  out.push_back(static_cast<uint8_t>((first[16] & 0xE0) | (first[17] & 0x1F)));
  for (const auto& packet : packets) {
    const auto bytes = PacketBytes(packet);
    out.insert(out.end(), bytes.begin() + 18, bytes.end());
  }
  return out;
}

}  // namespace

TEST(H264Source, SendsSmallFrameAsSinglePacket) {
  std::unique_ptr<H264Source> source(H264Source::CreateNew(25));
  std::vector<RtpPacket> packets;
  source->SetSendFrameCallback(
      [&](MediaChannelId, RtpPacket packet) {
        packets.push_back(packet);
        return true;
      });

  EXPECT_TRUE(source->HandleFrame(channel_0, MakeFrame(5, 0x65)));
  ASSERT_EQ(packets.size(), 1u);
  EXPECT_EQ(packets[0].size, 5u + 12u + 4u);
  EXPECT_EQ(packets[0].last, 1u);
  EXPECT_EQ(packets[0].timestamp, 9000u);
  EXPECT_EQ(packets[0].type, streamsight::rtsp::VIDEO_FRAME_I);

  const auto bytes = PacketBytes(packets[0]);
  EXPECT_EQ(bytes[16], 0x65);
}

TEST(H264Source, SendsExactlyMaxPayloadAsSinglePacket) {
  std::unique_ptr<H264Source> source(H264Source::CreateNew(25));
  std::vector<RtpPacket> packets;
  source->SetSendFrameCallback(
      [&](MediaChannelId, RtpPacket packet) {
        packets.push_back(packet);
        return true;
      });

  EXPECT_TRUE(source->HandleFrame(channel_0, MakeFrame(1420, 0x65)));
  EXPECT_EQ(packets.size(), 1u);
}

TEST(H264Source, FragmentsLargeFrameWithFuA) {
  std::unique_ptr<H264Source> source(H264Source::CreateNew(25));
  std::vector<RtpPacket> packets;
  source->SetSendFrameCallback(
      [&](MediaChannelId, RtpPacket packet) {
        packets.push_back(packet);
        return true;
      });

  const auto original = MakeFrame(1421, 0x65);
  EXPECT_TRUE(source->HandleFrame(channel_0, original));

  ASSERT_EQ(packets.size(), 2u);
  EXPECT_EQ(packets[0].last, 0u);
  EXPECT_EQ(packets[1].last, 1u);
  EXPECT_EQ(packets[0].size, 4u + 12u + 1420u);
  EXPECT_EQ(packets[1].size, 4u + 12u + 2u + 2u);

  const auto rebuilt = Reassemble(packets);
  const std::vector<uint8_t> expected(original.buffer.get(),
                                      original.buffer.get() + original.size);
  EXPECT_EQ(rebuilt, expected);
}

TEST(H264Source, ThreeFragmentsForLargeFrame) {
  std::unique_ptr<H264Source> source(H264Source::CreateNew(25));
  std::vector<RtpPacket> packets;
  source->SetSendFrameCallback(
      [&](MediaChannelId, RtpPacket packet) {
        packets.push_back(packet);
        return true;
      });

  EXPECT_TRUE(source->HandleFrame(channel_0, MakeFrame(2838, 0x65)));
  EXPECT_EQ(packets.size(), 3u);
}

TEST(H264Source, StopsSendingWhenCallbackFails) {
  std::unique_ptr<H264Source> source(H264Source::CreateNew(25));
  int calls = 0;
  source->SetSendFrameCallback([&](MediaChannelId, RtpPacket) {
    ++calls;
    return false;
  });

  EXPECT_FALSE(source->HandleFrame(channel_0, MakeFrame(2838, 0x65)));
  EXPECT_EQ(calls, 1);
}

TEST(RtcpMessage, BuildsSenderReport) {
  uint8_t buf[RTCP_SR_SIZE] = {0};
  ASSERT_EQ(BuildRtcpSR(buf, 0x11223344u, 90000u, 7u, 1400u),
            static_cast<int>(RTCP_SR_SIZE));

  RtcpSRPacket sr;
  std::memcpy(&sr, buf, RTCP_SR_SIZE);

  EXPECT_EQ(sr.header.version, 2);
  EXPECT_EQ(sr.header.pt, 200);
  EXPECT_EQ(ntohs(sr.header.length), 6);
  EXPECT_EQ(ntohl(sr.ssrc), 0x11223344u);
  EXPECT_EQ(ntohl(sr.rtp_ts), 90000u);
  EXPECT_EQ(ntohl(sr.pkt_count), 7u);
  EXPECT_EQ(ntohl(sr.octet_count), 1400u);
  EXPECT_GT(GetNtpTimestamp() >> 32, 2208988800ULL);
}
