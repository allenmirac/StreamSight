// RtmpOutputAdapter.h
// Output adapter that muxes encoded packets to RTMP via FFmpeg muxer.

#ifndef STREAMSIGHT_FFMPEG_RTMP_OUTPUT_ADAPTER_H
#define STREAMSIGHT_FFMPEG_RTMP_OUTPUT_ADAPTER_H

#include <string>

#include "IOutputAdapter.h"

struct AVFormatContext;
struct AVStream;

namespace streamsight::ffmpeg {

class RtmpOutputAdapter : public IOutputAdapter {
 public:
  explicit RtmpOutputAdapter(const std::string& rtmp_url);
  ~RtmpOutputAdapter() override;

  bool Open(const AVCodecContext* enc_ctx, const OutputConfig& cfg) override;
  void Close() override;
  bool WritePacket(const AVPacket* pkt) override;
  bool IsOpened() const override;

 private:
  std::string rtmp_url_;
  AVFormatContext* ofmt_ctx_ = nullptr;
  AVStream* video_stream_ = nullptr;
  int64_t pts_counter_ = 0;
  bool opened_ = false;
};

}  // namespace streamsight::ffmpeg

#endif  // STREAMSIGHT_FFMPEG_RTMP_OUTPUT_ADAPTER_H
