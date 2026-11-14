#include "GopCache.h"

#include <algorithm>
#include <cstring>
#include <utility>

using namespace streamsight::rtsp;

namespace {

constexpr int kPureAudioGuessCount = 115;  // SRS: ~3s of 26ms audio packets
constexpr size_t kMaxParamSets = 4;
// RtpPacket owns a fixed 1600-byte buffer; never copy more than that.
constexpr size_t kMaxRtpPacketBytes = 1600;

const uint8_t* PayloadOf(const RtpPacket& pkt) {
  return pkt.data.get() + RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE;
}

size_t PayloadSize(const RtpPacket& pkt) {
  const size_t header = RTP_TCP_HEAD_SIZE + RTP_HEADER_SIZE;
  return pkt.size > header ? pkt.size - header : 0;
}

// Length of a 3-byte start code at p[i]; a 4-byte 00 00 00 01 is found one byte
// later, so this single rule covers both forms.
size_t StartCodeLenAt(const uint8_t* p, size_t size, size_t i) {
  if (i + 3 <= size && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
    return 3;
  }
  return 0;
}

// Index of the next start code at or after `from`, or `size` if none exists.
// A 00 00 03 escape is skipped together with its following byte so an
// emulation-prevention sequence can never be mistaken for a boundary.
size_t FindStartCode(const uint8_t* p, size_t size, size_t from) {
  size_t i = from;
  while (i + 3 <= size) {
    if (p[i] == 0 && p[i + 1] == 0) {
      if (p[i + 2] == 1) return i;
      if (p[i + 2] == 3) {
        i += 4;
        continue;
      }
    }
    ++i;
  }
  return size;
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
  const size_t copy_size = std::min<size_t>(pkt.size, kMaxRtpPacketBytes);
  out.pkt.size = static_cast<uint32_t>(copy_size);
  if (copy_size > 0) {
    std::memcpy(out.pkt.data.get(), pkt.data.get(), copy_size);
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

GopCache::AccessUnitScan GopCache::ScanAccessUnit(MediaType media_type,
                                                  const uint8_t* p,
                                                  size_t size) {
  AccessUnitScan scan;
  if ((media_type != H264 && media_type != H265) || size == 0) return scan;

  const size_t header_len = media_type == H265 ? 2 : 1;

  auto consider = [&](size_t header_pos) {
    if (header_pos + header_len > size) return;
    const uint8_t nal_type =
        media_type == H265
            ? static_cast<uint8_t>((p[header_pos] >> 1) & 0x3F)
            : static_cast<uint8_t>(p[header_pos] & 0x1F);
    const bool is_idr =
        media_type == H265 ? (nal_type == 19 || nal_type == 20) : nal_type == 5;
    if (is_idr) {
      scan.has_idr = true;
      return;
    }
    if (IsParameterSet(media_type, nal_type)) {
      if (!scan.has_param_set ||
          ParamRank(media_type, nal_type) <
              ParamRank(media_type, scan.param_nal_type)) {
        scan.has_param_set = true;
        scan.param_nal_type = nal_type;
      }
    }
  };

  const size_t first = FindStartCode(p, size, 0);
  if (first == size) {
    // No start code at all: the payload is a bare NAL unit.
    consider(0);
    return scan;
  }

  size_t pos = first;
  while (pos < size) {
    const size_t sc_len = StartCodeLenAt(p, size, pos);
    if (sc_len == 0) {
      pos = FindStartCode(p, size, pos + 1);
      continue;
    }
    consider(pos + sc_len);
    // Skip past the NAL header; the next scan finds the following start code.
    pos = FindStartCode(p, size, pos + sc_len + header_len);
  }
  return scan;
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
    // A single RTP packet may carry a whole access unit: the production
    // encoder (libx264 without AV_CODEC_FLAG_GLOBAL_HEADER) emits
    // SPS+PPS+SEI+IDR in one AVPacket and the output adapter prepends one
    // more start code before pushing the AU. Scanning only the first NAL
    // would misclassify such an AU as a parameter set and drop the following
    // P frames. Fragmented packets are never parameter sets and fall through
    // to the IDR/P grouping below.
    const bool is_fu = IsFuPacket(media_type, pkt);
    AccessUnitScan scan;
    if (!is_fu) {
      scan = ScanAccessUnit(media_type, PayloadOf(pkt), PayloadSize(pkt));
    }

    if (!is_fu && !scan.has_idr && scan.has_param_set) {
      // Parameter sets form a dependency chain ordered by rank (VPS -> SPS ->
      // PPS). A newly arrived set invalidates every cached set of the same or
      // higher rank: a fresh SPS/VPS starts a new configuration generation,
      // while a fresh PPS only replaces the previous PPS. Lower-rank sets that
      // the new one still depends on are kept. A parameter-set-only AU is
      // cached whole under its lowest-rank parameter type.
      const int rank = ParamRank(media_type, scan.param_nal_type);
      params_.erase(
          std::remove_if(params_.begin(), params_.end(),
                         [media_type, rank](const ParamSet& ps) {
                           return ParamRank(media_type, ps.nal_type) >= rank;
                         }),
          params_.end());
      if (params_.size() < kMaxParamSets) {
        params_.push_back(ParamSet{scan.param_nal_type, Copy(channel, pkt)});
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

    // Any IDR inside the AU starts a new GOP, even when the AU also carries
    // parameter sets and SEI NALs ahead of it.
    if (pkt.type == VIDEO_FRAME_I || (!is_fu && scan.has_idr)) {
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
  // SRS clear() resets the pure-audio counter as well; otherwise the counter
  // would stay over the threshold and clear on every subsequent audio packet.
  audio_after_last_video_ = 0;
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
