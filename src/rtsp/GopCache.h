#ifndef STREAMSIGHT_RTSP_GOP_CACHE_H
#define STREAMSIGHT_RTSP_GOP_CACHE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include "media.h"
#include "rtp.h"

namespace streamsight::rtsp {

struct CachedPacket {
  MediaChannelId channel = channel_0;
  RtpPacket pkt;
};

// SRS-style GOP cache: the latest parameter sets plus the last complete GOP.
// See SrsGopCache in srs_app_rtmp_source.cpp.
class GopCache {
 public:
  void SetEnabled(bool enabled);
  bool Enabled() const;

  // 0 means unlimited, matching SRS play.gop_cache_max_frames.
  void SetMaxFrames(size_t max_frames);
  void SetMaxBytes(size_t max_bytes);

  // Feed every packetized RTP packet in arrival order.
  void Store(MediaChannelId channel, MediaType media_type,
             const RtpPacket& pkt);

  // Parameter sets (VPS/SPS/PPS) followed by the cached GOP.
  std::vector<CachedPacket> Snapshot() const;

  // SRS clear(): drop the GOP, keep parameter sets.
  void Clear();
  // Also drop parameter sets; used when a source is replaced.
  void Reset();

  bool Empty() const;
  size_t FrameCount() const;
  size_t ByteCount() const;

 private:
  struct ParamSet {
    uint8_t nal_type = 0;
    CachedPacket packet;
  };

  static CachedPacket Copy(MediaChannelId channel, const RtpPacket& pkt);
  static bool IsFuPacket(MediaType media_type, const RtpPacket& pkt);
  static bool ParseNalType(MediaType media_type, const RtpPacket& pkt,
                           uint8_t* nal_type);
  static bool IsParameterSet(MediaType media_type, uint8_t nal_type);
  static int ParamRank(MediaType media_type, uint8_t nal_type);
  static bool IsVideoType(uint8_t frame_type);

  void StoreLocked(MediaChannelId channel, const MediaType& media_type,
                   const RtpPacket& pkt);
  void ClearGopLocked();
  void EnforceLimitsLocked();
  size_t ByteCountLocked() const;
  void ResetLocked();

  mutable std::mutex mutex_;
  bool enabled_ = true;
  size_t max_frames_ = 2500;
  size_t max_bytes_ = 8u * 1024u * 1024u;
  bool seen_video_ = false;
  int audio_after_last_video_ = 0;
  std::vector<ParamSet> params_;
  // Complete, replayable GOP.
  std::deque<CachedPacket> gop_;
  // Fragments of an IDR that has not seen last=1 yet. Not replayable.
  std::deque<CachedPacket> pending_idr_;
};

}  // namespace streamsight::rtsp

#endif
