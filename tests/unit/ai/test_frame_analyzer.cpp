#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

#include "ai/FrameAnalyzer.h"

using streamsight::ai::FrameAnalyzer;

TEST(FrameAnalyzer, NullDetectorReturnsEmptyResult) {
  FrameAnalyzer analyzer(nullptr, nullptr, nullptr);
  cv::Mat frame(16, 16, CV_8UC3, cv::Scalar(0, 0, 0));

  const auto result = analyzer.Analyze(frame);
  EXPECT_TRUE(result.faces.empty());
  EXPECT_EQ(result.frame_id, 1);
  EXPECT_GT(result.timestamp_ms, 0);
}

TEST(FrameAnalyzer, ZeroFpsAnalyzesEveryFrame) {
  FrameAnalyzer analyzer(nullptr, nullptr, nullptr);
  analyzer.SetAnalyzeRate(0);
  cv::Mat frame(16, 16, CV_8UC3, cv::Scalar(0, 0, 0));

  const auto first = analyzer.Analyze(frame);
  const auto second = analyzer.Analyze(frame);
  EXPECT_EQ(first.frame_id, 1);
  EXPECT_EQ(second.frame_id, 2);
}

TEST(FrameAnalyzer, RateLimitReusesCachedResult) {
  FrameAnalyzer analyzer(nullptr, nullptr, nullptr);
  analyzer.SetAnalyzeRate(1);
  cv::Mat frame(16, 16, CV_8UC3, cv::Scalar(0, 0, 0));

  const auto first = analyzer.Analyze(frame);
  const auto second = analyzer.Analyze(frame);
  EXPECT_EQ(first.frame_id, 1);
  EXPECT_EQ(second.frame_id, 1);
  EXPECT_EQ(analyzer.GetLastResult().frame_id, 1);
}
