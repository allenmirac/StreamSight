// SeiLatencyMarker.h
// Inject sender-side timing information into H.264 SEI NAL units
// for cross-machine end-to-end latency measurement.
//
// Usage (sender side):
//   auto marked = InjectLatencySei(nalu_data, nalu_len, frame_id);
//   // Push marked.data() into RTP pipeline
//
// Receiver reads SEI payload to compute:
//   e2e_delay = arrival_time - send_time_us - clock_offset

#ifndef XOP_SEI_LATENCY_MARKER_H
#define XOP_SEI_LATENCY_MARKER_H

#include <cstdint>
#include <vector>
#include <chrono>
#include <cstring>

namespace xop {

static const char kSeiUuid[] = "STRMSYNC";  // 8-byte UUID for StreamSight latency SEI

struct SeiLatencyPayload {
    char     uuid[8];         // "STRMSYNC"
    uint64_t frame_id;        // sequential frame counter
    uint64_t send_time_us;    // sender steady_clock absolute microseconds
    uint32_t encode_time_us;  // encoding latency in microseconds
};

// Build a single H.264 SEI NALU (type 6) containing latency payload.
// Returns the complete Annex-B NALU: [0x00 0x00 0x00 0x01][NAL header][SEI payload][RBSP trailing]
// Caller appends this before the IDR NALU in the same RTP packet or as a separate packet.
inline std::vector<uint8_t> BuildLatencySeiNalu(
    uint64_t frame_id, uint64_t send_time_us, uint32_t encode_time_us)
{
    SeiLatencyPayload payload;
    memcpy(payload.uuid, kSeiUuid, 8);
    payload.frame_id      = frame_id;
    payload.send_time_us  = send_time_us;
    payload.encode_time_us = encode_time_us;

    // SEI payload type = 5 (user_data_unregistered)
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&payload);
    size_t payload_size = sizeof(SeiLatencyPayload);

    // Build NALU: Annex-B start code + NAL header (type 6) + SEI data
    // SEI data = payload_type(1B) + payload_size(1B) + uuid(16B) + user_data
    // Simplified: we write type=5, size=payload_size, then the raw struct
    size_t sei_data_size = 1 + 1 + payload_size;   // type + size + payload
    size_t nalu_size = 4 + 1 + sei_data_size;      // start code + NAL header + SEI data

    std::vector<uint8_t> nalu(nalu_size);
    nalu[0] = 0x00; nalu[1] = 0x00; nalu[2] = 0x00; nalu[3] = 0x01; // Annex-B start
    nalu[4] = 0x06;  // NAL unit type = SEI

    size_t off = 5;
    nalu[off++] = 5;                 // SEI payload type = user_data_unregistered
    nalu[off++] = (uint8_t)payload_size;
    memcpy(nalu.data() + off, p, payload_size);
    off += payload_size;
    nalu[off++] = 0x80;  // RBSP trailing bits

    return nalu;
}

// Convenience: generate send timestamp using steady_clock now.
inline uint64_t GetSendTimeUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace xop

#endif // XOP_SEI_LATENCY_MARKER_H
