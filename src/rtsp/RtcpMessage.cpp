// RtcpMessage.cpp
// RTCP packet construction and NTP timestamp utilities.

#include "RtcpMessage.h"

#include <chrono>
#include <cstring>

extern "C" {
#include <arpa/inet.h>
}

namespace streamsight::rtsp {

// ─── NTP timestamp ──────────────────────────────────────────────────────

// NTP epoch offset: 2208988800 = seconds between Jan 1 1900 and Jan 1 1970.
static constexpr uint64_t kNtpEpochOffset = 2208988800ULL;

uint64_t GetNtpTimestamp() {
  using namespace std::chrono;

  auto now = system_clock::now();
  auto usec = duration_cast<microseconds>(now.time_since_epoch()).count();

  uint64_t sec = (uint64_t)(usec / 1000000) + kNtpEpochOffset;
  // Convert remaining microseconds to 32-bit NTP fraction:
  //   fraction = (usec % 1e6) * 2^32 / 1e6
  uint32_t frac = (uint32_t)(((usec % 1000000) * 4294967296ULL) / 1000000);

  return (sec << 32) | frac;
}

// ─── Sender Report builder ──────────────────────────────────────────────

int BuildRtcpSR(uint8_t* buf, uint32_t ssrc, uint32_t rtp_ts,
                uint32_t pkt_count, uint32_t octet_count) {
  RtcpSRPacket sr;
  sr.header.version = RTCP_VERSION;
  sr.header.padding = 0;
  sr.header.rc = 0;  // no report blocks
  sr.header.pt = (uint8_t)RtcpPacketType::SR;
  sr.header.length = htons(6);  // (28/4) - 1 = 6

  sr.ssrc = htonl(ssrc);

  uint64_t ntp = GetNtpTimestamp();
  sr.ntp_msw = htonl((uint32_t)(ntp >> 32));
  sr.ntp_lsw = htonl((uint32_t)(ntp & 0xFFFFFFFF));
  sr.rtp_ts = htonl(rtp_ts);
  sr.pkt_count = htonl(pkt_count);
  sr.octet_count = htonl(octet_count);

  memcpy(buf, &sr, RTCP_SR_SIZE);
  return (int)RTCP_SR_SIZE;
}

// ─── RTCP reception ─────────────────────────────────────────────────────

namespace {

uint32_t ReadU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

bool ReadReportBlocks(const uint8_t* p, size_t bytes, uint8_t count,
                      std::vector<RtcpReportBlock>& out) {
  if (bytes < static_cast<size_t>(count) * 24) return false;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t* b = p + static_cast<size_t>(i) * 24;
    RtcpReportBlock rb;
    rb.ssrc = ReadU32(b);
    rb.fraction_lost = b[4];
    const uint32_t lost = (static_cast<uint32_t>(b[5]) << 16) |
                          (static_cast<uint32_t>(b[6]) << 8) | b[7];
    rb.cumulative_lost =
        (lost & 0x800000u)
            ? static_cast<int32_t>(static_cast<int64_t>(lost) - 0x1000000LL)
            : static_cast<int32_t>(lost);
    rb.extended_highest_seq = ReadU32(b + 8);
    rb.jitter = ReadU32(b + 12);
    rb.lsr = ReadU32(b + 16);
    rb.dlsr = ReadU32(b + 20);
    out.push_back(rb);
  }
  return true;
}

}  // namespace

bool IsRtcp(const uint8_t* data, size_t len) {
  return data != nullptr && len >= 12 && (data[0] >> 6) == RTCP_VERSION &&
         data[1] >= 192 && data[1] <= 223;
}

bool ParseRtcpCompound(const uint8_t* data, size_t len, RtcpParseResult& out) {
  if (data == nullptr) return false;
  size_t off = 0;
  bool got_any = false;
  while (off + 4 <= len) {
    const uint8_t b0 = data[off];
    if ((b0 >> 6) != RTCP_VERSION) return false;
    const bool padding = (b0 >> 5) & 1;
    const uint8_t count = b0 & 0x1F;
    const uint8_t pt = data[off + 1];
    const uint32_t words =
        (static_cast<uint32_t>(data[off + 2]) << 8) | data[off + 3];
    const size_t packet_bytes = (static_cast<size_t>(words) + 1) * 4;
    if (packet_bytes < 8 || off + packet_bytes > len) return false;

    size_t effective = packet_bytes;
    if (padding) {
      const uint8_t pad = data[off + packet_bytes - 1];
      if (pad == 0 || pad > packet_bytes - 4) return false;
      effective -= pad;
    }

    switch (pt) {
      case RTCP_PT_SR:
        if (effective < 28) return false;
        ++out.sr_count;
        if (!ReadReportBlocks(data + off + 28, effective - 28, count,
                              out.report_blocks)) {
          return false;
        }
        break;
      case RTCP_PT_RR:
        if (effective < 8) return false;
        ++out.rr_count;
        if (!ReadReportBlocks(data + off + 8, effective - 8, count,
                              out.report_blocks)) {
          return false;
        }
        break;
      case RTCP_PT_SDES:
        ++out.sdes_count;
        break;
      case RTCP_PT_BYE:
        ++out.bye_count;
        break;
      case 204:  // APP
      case 205:  // RTPFB
      case 206:  // PSFB
        break;
      default:
        return false;
    }
    got_any = true;
    off += packet_bytes;
  }
  return got_any && off == len;
}

uint32_t ComputeRttUs(uint32_t lsr, uint32_t dlsr, uint32_t now_ntp_middle) {
  if (lsr == 0) return 0;
  const uint32_t delay = now_ntp_middle - lsr - dlsr;  // 1/65536 s units
  return static_cast<uint32_t>(static_cast<uint64_t>(delay) * 1000000ULL /
                               65536ULL);
}

}  // namespace streamsight::rtsp
