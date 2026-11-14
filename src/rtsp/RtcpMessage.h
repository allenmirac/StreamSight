// RtcpMessage.h
// RTCP packet types, structures, and builder utilities.
// RFC 3550 — RTP Control Protocol.

#ifndef STREAMSIGHT_RTSP_RTCP_MESSAGE_H
#define STREAMSIGHT_RTSP_RTCP_MESSAGE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rtp.h"

namespace streamsight::rtsp {

// RTCP packet types (RFC 3550 Section 13)
enum class RtcpPacketType : uint8_t {
  SR = 200,    // Sender Report
  RR = 201,    // Receiver Report
  SDES = 202,  // Source Description
  BYE = 203,   // Goodbye
  APP = 204,   // Application-defined
};

// RTCP common header (4 bytes) — precedes every RTCP packet.
// Bit-fields are laid out for little-endian host (matched to RtpHeader
// convention).
#pragma pack(push, 1)
struct RtcpHeader {
#if RTP_HEADER_BIG_ENDIAN
  uint8_t version : 2;
  uint8_t padding : 1;
  uint8_t rc : 5;  // report count (SR/RR) or source count (SDES/BYE)
#else
  uint8_t rc : 5;
  uint8_t padding : 1;
  uint8_t version : 2;
#endif
  uint8_t pt;       // packet type
  uint16_t length;  // number of 32-bit words minus 1 (network byte order)
};
#pragma pack(pop)

static const uint8_t RTCP_VERSION = 2;

// RTCP Sender Report structure (28 bytes total)
#pragma pack(push, 1)
struct RtcpSRPacket {
  RtcpHeader header;     // version=2, pt=200, length=6
  uint32_t ssrc;         // sender SSRC
  uint32_t ntp_msw;      // NTP timestamp — most significant word (seconds)
  uint32_t ntp_lsw;      // NTP timestamp — least significant word (fraction)
  uint32_t rtp_ts;       // corresponding RTP timestamp
  uint32_t pkt_count;    // cumulative packets sent
  uint32_t octet_count;  // cumulative payload bytes sent (not including
                         // RTP/UDP/IP headers)
};
#pragma pack(pop)

static const size_t RTCP_SR_SIZE = 28;
static const size_t RTCP_HEADER_SIZE = 4;

// ─── Builder functions ──────────────────────────────────────────────────

// Convert Unix epoch (1970) to NTP epoch (1900) 64-bit timestamp.
// Returns the compact NTP format used in RTCP (seconds<<32 | fraction).
uint64_t GetNtpTimestamp();

// Build an RTCP Sender Report into buf (caller provides at least 28 bytes).
// Returns the number of bytes written (always 28).
int BuildRtcpSR(uint8_t* buf, uint32_t ssrc, uint32_t rtp_ts,
                uint32_t pkt_count, uint32_t octet_count);

// ─── RTCP reception (RFC 3550) ──────────────────────────────────────────

static const uint8_t RTCP_PT_SR = 200;
static const uint8_t RTCP_PT_RR = 201;
static const uint8_t RTCP_PT_SDES = 202;
static const uint8_t RTCP_PT_BYE = 203;

struct RtcpReportBlock {
  uint32_t ssrc = 0;
  uint8_t fraction_lost = 0;
  int32_t cumulative_lost = 0;  // 24-bit signed, sign-extended
  uint32_t extended_highest_seq = 0;
  uint32_t jitter = 0;
  uint32_t lsr = 0;
  uint32_t dlsr = 0;
};

struct RtcpParseResult {
  uint32_t sr_count = 0;
  uint32_t rr_count = 0;
  uint32_t sdes_count = 0;
  uint32_t bye_count = 0;
  std::vector<RtcpReportBlock> report_blocks;
};

// SRS-compatible RTCP detection: full header, version 2, PT in [192, 223].
bool IsRtcp(const uint8_t* data, size_t len);

// Walk one compound RTCP packet. Returns false on malformed input.
bool ParseRtcpCompound(const uint8_t* data, size_t len, RtcpParseResult& out);

// RTT in microseconds from RTCP RR LSR/DLSR and the current NTP middle 32
// bits. Returns 0 when lsr is 0 (no SR received yet).
uint32_t ComputeRttUs(uint32_t lsr, uint32_t dlsr, uint32_t now_ntp_middle);

}  // namespace streamsight::rtsp

#endif  // STREAMSIGHT_RTSP_RTCP_MESSAGE_H
