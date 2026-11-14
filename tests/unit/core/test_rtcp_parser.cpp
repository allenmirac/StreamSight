#include <arpa/inet.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "rtsp/RtcpMessage.h"

using streamsight::rtsp::BuildRtcpSR;
using streamsight::rtsp::ComputeRttUs;
using streamsight::rtsp::IsRtcp;
using streamsight::rtsp::ParseRtcpCompound;
using streamsight::rtsp::RTCP_SR_SIZE;
using streamsight::rtsp::RtcpParseResult;

namespace {

void WriteU32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

std::vector<uint8_t> BuildRr(uint8_t rc, uint32_t block_ssrc, uint8_t fraction,
                             int32_t lost, uint32_t jitter, uint32_t lsr,
                             uint32_t dlsr) {
  std::vector<uint8_t> out(8 + static_cast<size_t>(rc) * 24, 0);
  out[0] = static_cast<uint8_t>(0x80 | rc);
  out[1] = 201;
  const uint16_t words = static_cast<uint16_t>(out.size() / 4 - 1);
  out[2] = static_cast<uint8_t>(words >> 8);
  out[3] = static_cast<uint8_t>(words);
  WriteU32(out.data() + 4, 0x01020304u);
  uint8_t* b = out.data() + 8;
  WriteU32(b, block_ssrc);
  b[4] = fraction;
  const uint32_t lost24 = static_cast<uint32_t>(lost) & 0xFFFFFFu;
  b[5] = static_cast<uint8_t>(lost24 >> 16);
  b[6] = static_cast<uint8_t>(lost24 >> 8);
  b[7] = static_cast<uint8_t>(lost24);
  WriteU32(b + 8, 0x00012345u);
  WriteU32(b + 12, jitter);
  WriteU32(b + 16, lsr);
  WriteU32(b + 20, dlsr);
  return out;
}

}  // namespace

TEST(RtcpParser, DetectsRtcpPackets) {
  uint8_t sr[RTCP_SR_SIZE] = {0};
  BuildRtcpSR(sr, 1, 2, 3, 4);
  EXPECT_TRUE(IsRtcp(sr, RTCP_SR_SIZE));
  EXPECT_FALSE(IsRtcp(sr, 4));
  sr[1] = 96;  // RTP payload type
  EXPECT_FALSE(IsRtcp(sr, RTCP_SR_SIZE));
}

TEST(RtcpParser, ParsesSenderReport) {
  uint8_t sr[RTCP_SR_SIZE] = {0};
  ASSERT_EQ(BuildRtcpSR(sr, 0x11223344u, 90000u, 7u, 1400u),
            static_cast<int>(RTCP_SR_SIZE));
  RtcpParseResult out;
  ASSERT_TRUE(ParseRtcpCompound(sr, RTCP_SR_SIZE, out));
  EXPECT_EQ(out.sr_count, 1u);
  EXPECT_EQ(out.rr_count, 0u);
  EXPECT_TRUE(out.report_blocks.empty());
}

TEST(RtcpParser, ParsesReceiverReportBlock) {
  const auto rr = BuildRr(1, 0xAABBCCDDu, 10, -3, 100, 0x12345678u, 0x8000u);
  RtcpParseResult out;
  ASSERT_TRUE(ParseRtcpCompound(rr.data(), rr.size(), out));
  EXPECT_EQ(out.rr_count, 1u);
  ASSERT_EQ(out.report_blocks.size(), 1u);
  const auto& b = out.report_blocks[0];
  EXPECT_EQ(b.ssrc, 0xAABBCCDDu);
  EXPECT_EQ(b.fraction_lost, 10u);
  EXPECT_EQ(b.cumulative_lost, -3);
  EXPECT_EQ(b.extended_highest_seq, 0x00012345u);
  EXPECT_EQ(b.jitter, 100u);
  EXPECT_EQ(b.lsr, 0x12345678u);
  EXPECT_EQ(b.dlsr, 0x8000u);
}

TEST(RtcpParser, ParsesCompoundRrAndSdes) {
  auto rr = BuildRr(1, 7, 1, 2, 3, 4, 5);
  std::vector<uint8_t> sdes(8, 0);
  sdes[0] = 0x81;  // V=2, SC=1
  sdes[1] = 202;
  sdes[2] = 0;
  sdes[3] = 1;  // 8 bytes total
  WriteU32(sdes.data() + 4, 7);
  std::vector<uint8_t> compound = rr;
  compound.insert(compound.end(), sdes.begin(), sdes.end());
  RtcpParseResult out;
  ASSERT_TRUE(ParseRtcpCompound(compound.data(), compound.size(), out));
  EXPECT_EQ(out.rr_count, 1u);
  EXPECT_EQ(out.sdes_count, 1u);
  EXPECT_EQ(out.report_blocks.size(), 1u);
}

TEST(RtcpParser, ParsesMultipleReportBlocks) {
  std::vector<uint8_t> rr = BuildRr(1, 1, 0, 0, 0, 0, 0);
  const auto second = BuildRr(1, 2, 0, 0, 0, 0, 0);
  rr.insert(rr.end(), second.begin() + 8, second.end());  // append a block
  rr[0] = 0x82;                                           // RC=2
  const uint16_t words = static_cast<uint16_t>(rr.size() / 4 - 1);
  rr[2] = static_cast<uint8_t>(words >> 8);
  rr[3] = static_cast<uint8_t>(words);
  RtcpParseResult out;
  ASSERT_TRUE(ParseRtcpCompound(rr.data(), rr.size(), out));
  ASSERT_EQ(out.report_blocks.size(), 2u);
  EXPECT_EQ(out.report_blocks[0].ssrc, 1u);
  EXPECT_EQ(out.report_blocks[1].ssrc, 2u);
}

TEST(RtcpParser, RejectsMalformedPackets) {
  RtcpParseResult out;
  const auto rr = BuildRr(1, 1, 0, 0, 0, 0, 0);
  EXPECT_FALSE(ParseRtcpCompound(rr.data(), rr.size() - 1, out));
  EXPECT_FALSE(ParseRtcpCompound(rr.data(), 4, out));
  auto bad_version = rr;
  bad_version[0] = 0x41;  // V=1
  EXPECT_FALSE(ParseRtcpCompound(bad_version.data(), bad_version.size(), out));
  auto bad_type = rr;
  bad_type[1] = 99;
  EXPECT_FALSE(ParseRtcpCompound(bad_type.data(), bad_type.size(), out));
}

TEST(RtcpParser, ComputesRttWithoutWrap) {
  const uint32_t lsr = (10u << 16) | 0x8000u;
  const uint32_t dlsr = 0x8000u;  // 0.5s
  const uint32_t now = lsr + dlsr + 0x8000u;
  EXPECT_EQ(ComputeRttUs(lsr, dlsr, now), 500000u);
}

TEST(RtcpParser, ComputesRttAcrossWrap) {
  const uint32_t lsr = 0xFFFF0000u;
  const uint32_t dlsr = 0x8000u;
  const uint32_t now = 0x00000000u;  // wrapped
  EXPECT_EQ(ComputeRttUs(lsr, dlsr, now), 500000u);
}

TEST(RtcpParser, NoRttWithoutSenderReport) {
  EXPECT_EQ(ComputeRttUs(0, 0x1000, 0xFFFFFFFFu), 0u);
}
