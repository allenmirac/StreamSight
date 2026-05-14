// RtcpMessage.cpp
// RTCP packet construction and NTP timestamp utilities.

#include "RtcpMessage.h"
#include <chrono>
#include <cstring>

extern "C" {
#include <arpa/inet.h>
}

namespace xop {

// ─── NTP timestamp ──────────────────────────────────────────────────────

// NTP epoch offset: 2208988800 = seconds between Jan 1 1900 and Jan 1 1970.
static constexpr uint64_t kNtpEpochOffset = 2208988800ULL;

uint64_t GetNtpTimestamp() {
    using namespace std::chrono;

    auto now = system_clock::now();
    auto usec = duration_cast<microseconds>(now.time_since_epoch()).count();

    uint64_t sec  = (uint64_t)(usec / 1000000) + kNtpEpochOffset;
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
    sr.header.rc      = 0;         // no report blocks
    sr.header.pt      = (uint8_t)RtcpPacketType::SR;
    sr.header.length  = htons(6);  // (28/4) - 1 = 6

    sr.ssrc        = htonl(ssrc);

    uint64_t ntp = GetNtpTimestamp();
    sr.ntp_msw    = htonl((uint32_t)(ntp >> 32));
    sr.ntp_lsw    = htonl((uint32_t)(ntp & 0xFFFFFFFF));
    sr.rtp_ts     = htonl(rtp_ts);
    sr.pkt_count  = htonl(pkt_count);
    sr.octet_count = htonl(octet_count);

    memcpy(buf, &sr, RTCP_SR_SIZE);
    return (int)RTCP_SR_SIZE;
}

} // namespace xop
