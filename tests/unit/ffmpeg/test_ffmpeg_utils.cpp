#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <vector>

#include "ffmpeg/FFmpegUtils.h"

using streamsight::ffmpeg::AvRationalToDouble;
using streamsight::ffmpeg::av_err_str;
using streamsight::ffmpeg::FFmpegFrame;
using streamsight::ffmpeg::FrameToMat;
using streamsight::ffmpeg::PtsTo90kHz;

TEST(FFmpegUtils, ErrorStringIsNotEmpty) {
  EXPECT_FALSE(av_err_str(AVERROR(EINVAL)).empty());
}

TEST(FFmpegUtils, ConvertsPtsTo90kHz) {
  EXPECT_EQ(PtsTo90kHz(0, 25), 0u);
  EXPECT_EQ(PtsTo90kHz(25, 25), 90000u);
  EXPECT_EQ(PtsTo90kHz(1, 25), 3600u);
}

TEST(FFmpegUtils, ConvertsRationalToDouble) {
  EXPECT_DOUBLE_EQ(AvRationalToDouble(AVRational{1, 25}), 0.04);
  EXPECT_DOUBLE_EQ(AvRationalToDouble(AVRational{0, 0}), 0.0);
}

TEST(FFmpegUtils, WrapsFrameDataWithoutCopy) {
  constexpr int kWidth = 4;
  constexpr int kHeight = 3;
  constexpr int kLinesize = 16;

  std::vector<uint8_t> data(
      static_cast<size_t>(kLinesize) * kHeight, 0x7F);
  FFmpegFrame frame{kWidth, kHeight, kLinesize, data.data(), 0, 0, false, 0};

  cv::Mat mat = FrameToMat(frame);
  EXPECT_EQ(mat.cols, kWidth);
  EXPECT_EQ(mat.rows, kHeight);
  EXPECT_EQ(mat.step, static_cast<size_t>(kLinesize));
  EXPECT_EQ(mat.data, data.data());
  EXPECT_EQ(mat.at<cv::Vec3b>(0, 0)[0], 0x7F);
}
