#include "GopCache.h"

#include <algorithm>
#include <cstring>
#include <utility>

using namespace streamsight::rtsp;

namespace {

constexpr int kPureAudioGuessCount = 115;  // SRS: ~3s of 26ms audio packets
constexpr size_t kMaxParamSets = 4;

const uint8_t* PayloadOf(const RtpPacket& pkt) {
  return pkt.data.get() + RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE;
}

size_t PayloadSize(const RtpPacket& pkt) {
  const size_t header = RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE;
  return pkt.size > header ? pkt.size - header : 0;
}

size_t SkipStartCode(const uint8_t* p, size_t size) {
  if (size >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
    return 4;
  }
  if (size >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1) {
    return 3;
  }
  return 0;
}

}  // namespace

void GopCache::SetEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_ = enabled;
  if (!enabled) ClearGopLocked();
}

bool GopCache::Enabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return enabled_;
}

void GopCache::SetMaxFrames(size_t max_frames) {
  std::lock_guard<std::mutex> lock(mutex_);
  max_frames_ = max_frames;
  EnforceLimitsLocked();
}

void GopCache::SetMaxBytes(size_t max_bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  max_bytes_ = max_bytes;
  EnforceLimitsLocked();
}

CachedPacket GopCache::Copy(MediaChannelId channel, const RtpPacket& pkt) {
  CachedPacket out;
  out.channel = channel;
  out.pkt.type = pkt.type;
  out.pkt.last = pkt.last;
  out.pkt.timestamp = pkt.timestamp;
  out.pkt.size = pkt.size;
  if (pkt.size > 0) {
    std::memcpy(out.pkt.data.get(), pkt.data.get(), pkt.size);
  }
  return out;
}

bool GopCache::IsVideoType(uint8_t frame_type) {
  return frame_type == VIDEO_FRAME_I || frame_type == VIDEO_FRAME_P ||
         frame_type == VIDEO_FRAME_B;
}

bool GopCache::IsFuPacket(MediaType media_type, const RtpPacket& pkt) {
  const uint8_t* p = PayloadOf(pkt);
  const size_t size = PayloadSize(pkt);
  if (size < 1) return false;
  if (media_type == H264) return (p[0] & 0x1F) == 28;
  if (media_type == H265) return ((p[0] >> 1) & 0x3F) == 49;
  return false;
}

bool GopCache::ParseNalType(MediaType media_type, const RtpPacket& pkt,
                            uint8_t* nal_type) {
  const uint8_t* p = PayloadOf(pkt);
  const size_t size = PayloadSize(pkt);
  if (size == 0) return false;

  if (media_type == H264) {
    if ((p[0] & 0x1F) == 28 && size >= 2) {
      *nal_type = p[1] & 0x1F;
      return true;
    }
    const size_t off = SkipStartCode(p, size);
    if (off >= size) return false;
    *nal_type = p[off] & 0x1F;
    return true;
  }

  if (media_type == H265) {
    if (size >= 3 && ((p[0] >> 1) & 0x3F) == 49) {
      *nal_type = p[2] & 0x3F;
      return true;
    }
    const size_t off = SkipStartCode(p, size);
    if (off + 1 >= size) return false;
    *nal_type = (p[off] >> 1) & 0x3F;
    return true;
  }

  return false;
}

bool GopCache::IsParameterSet(MediaType media_type, uint8_t nal_type) {
  if (media_type == H264) return nal_type == 7 || nal_type == 8;
  if (media_type == H265) {
    return nal_type == 32 || nal_type == 33 || nal_type == 34;
  }
  return false;
}

int GopCache::ParamRank(MediaType media_type, uint8_t nal_type) {
  if (media_type == H265) {
    if (nal_type == 32) return 0;
    if (nal_type == 33) return 1;
    if (nal_type == 34) return 2;
  }
  if (media_type == H264) {
    if (nal_type == 7) return 0;
    if (nal_type == 8) return 1;
  }
  return 3;
}

void GopCache::Store(MediaChannelId channel, MediaType media_type,
                     const RtpPacket& pkt) {
  std::lock_guard<std::mutex> lock(mutex_);
  StoreLocked(channel, media_type, pkt);
}

void GopCache::StoreLocked(MediaChannelId channel, const MediaType& media_type,
                           const RtpPacket& pkt) {
  if (!enabled_) return;

  const bool video = IsVideoType(pkt.type);
  const bool audio = (pkt.type == AUDIO_FRAME);

  if (video) {
    if (media_type != H264 && media_type != H265) return;
    seen_video_ = true;
    audio_after_last_video_ = 0;
  } else if (audio) {
    if (!seen_video_) return;
    ++audio_after_last_video_;
    if (audio_after_last_video_ > kPureAudioGuessCount) {
      ClearGopLocked();
      return;
    }
  } else {
    return;
  }

  if (video) {
    uint8_t nal_type = 0;
    const bool has_nal = ParseNalType(media_type, pkt, &nal_type);
    // Parameter sets are expected to be single packets. A fragmented packet
    // is never treated as a parameter set; it falls through to the IDR/P
    // grouping below.
    const bool is_fu = IsFuPacket(media_type, pkt);
    if (!is_fu && has_nal && IsParameterSet(media_type, nal_type)) {
      // Parameter sets form a dependency chain ordered by rank (VPS -> SPS ->
      // PPS). A newly arrived set invalidates every cached set of the same or
      // higher rank: a fresh SPS/VPS starts a new configuration generation,
      // while a fresh PPS only replaces the previous PPS. Lower-rank sets that
      // the new one still depends on are kept.
      const int rank = ParamRank(media_type, nal_type);
      params_.erase(
          std::remove_if(params_.begin(), params_.end(),
                         [media_type, rank](const ParamSet& ps) {
                           return ParamRank(media_type, ps.nal_type) >= rank;
                         }),
          params_.end());
      if (params_.size() < kMaxParamSets) {
        params_.push_back(ParamSet{nal_type, Copy(channel, pkt)});
        std::stable_sort(params_.begin(), params_.end(),
                         [media_type](const ParamSet& a, const ParamSet& b) {
                           return ParamRank(media_type, a.nal_type) <
                                  ParamRank(media_type, b.nal_type);
                         });
      }
      // A new configuration invalidates the previous GOP.
      ClearGopLocked();
      EnforceLimitsLocked();
      return;
    }

    if (pkt.type == VIDEO_FRAME_I) {
      if (!pending_idr_.empty()) {
        // Continuation of a fragmented IDR.
        pending_idr_.push_back(Copy(channel, pkt));
        if (pkt.last != 0) {
          gop_ = std::move(pending_idr_);
          pending_idr_.clear();
        }
      } else if (pkt.last != 0) {
        // Single-packet IDR starts a new GOP.
        ClearGopLocked();
        gop_.push_back(Copy(channel, pkt));
      } else {
        // First fragment of a new IDR; do not publish it until last=1.
        ClearGopLocked();
        pending_idr_.push_back(Copy(channel, pkt));
      }
      EnforceLimitsLocked();
      return;
    }

    // A P/B frame interrupting an unfinished IDR invalidates that IDR.
    if (!pending_idr_.empty()) {
      pending_idr_.clear();
      return;
    }

    if (!gop_.empty()) {
      gop_.push_back(Copy(channel, pkt));
      EnforceLimitsLocked();
    }
    return;
  }

  // Audio is only useful once a replayable GOP exists. Ignore audio while an
  // IDR is still being assembled.
  if (audio && pending_idr_.empty() && !gop_.empty()) {
    gop_.push_back(Copy(channel, pkt));
    EnforceLimitsLocked();
  }
}

std::vector<CachedPacket> GopCache::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<CachedPacket> out;
  out.reserve(params_.size() + gop_.size());
  // Deep-copy: callers rewrite RTP headers per connection.
  for (const ParamSet& ps : params_) {
    out.push_back(Copy(ps.packet.channel, ps.packet.pkt));
  }
  for (const CachedPacket& pkt : gop_) {
    out.push_back(Copy(pkt.channel, pkt.pkt));
  }
  return out;
}

void GopCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  ClearGopLocked();
}

void GopCache::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  ResetLocked();
}

void GopCache::ClearGopLocked() {
  gop_.clear();
  pending_idr_.clear();
}

void GopCache::ResetLocked() {
  ClearGopLocked();
  params_.clear();
  seen_video_ = false;
  audio_after_last_video_ = 0;
}

void GopCache::EnforceLimitsLocked() {
  if (max_frames_ > 0 && gop_.size() > max_frames_) {
    ClearGopLocked();
    return;
  }
  if (max_bytes_ > 0 && ByteCountLocked() > max_bytes_) {
    ClearGopLocked();
  }
}

size_t GopCache::ByteCountLocked() const {
  size_t total = 0;
  for (const ParamSet& ps : params_) total += ps.packet.pkt.size;
  for (const CachedPacket& pkt : gop_) total += pkt.pkt.size;
  for (const CachedPacket& pkt : pending_idr_) total += pkt.pkt.size;
  return total;
}

bool GopCache::Empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return gop_.empty() && params_.empty();
}

size_t GopCache::FrameCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  // Replayable packets only: parameter sets + committed GOP.
  // pending_idr_ is deliberately excluded until it becomes complete.
  return params_.size() + gop_.size();
}

size_t GopCache::ByteCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ByteCountLocked();
}
