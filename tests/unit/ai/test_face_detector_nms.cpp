#include <gtest/gtest.h>
#include <opencv2/core.hpp>

#include <string>
#include <vector>

#include "ai/FaceDetectorDetail.h"

using streamsight::ai::FaceBox;
using streamsight::ai::detail::DecodeYuNet;
using streamsight::ai::detail::Nms;

namespace {

FaceBox Box(float x, float y, float w, float h, float confidence) {
  FaceBox box;
  box.rect = cv::Rect2f(x, y, w, h);
  box.confidence = confidence;
  return box;
}

struct YuNetOutputs {
  int stride = 8;
  int input = 320;
  int width = 320;
  int height = 320;
  int frame_width = 640;
  int frame_height = 480;
  int cell = 0;
  float score = 0.9f;
  cv::Mat cls;
  cv::Mat obj;
  cv::Mat bbox;
  cv::Mat kps;
  std::vector<cv::Mat> outputs;
  std::vector<std::string> names;

  YuNetOutputs() {
    const int fw = input / stride;
    const int fh = input / stride;
    const int n = fw * fh;
    cell = 20 * fw + 20;

    cls = cv::Mat::zeros(1, n, CV_32F);
    obj = cv::Mat::zeros(1, n, CV_32F);
    bbox = cv::Mat::zeros(1, n * 4, CV_32F);
    kps = cv::Mat::zeros(1, n * 10, CV_32F);
    cls.at<float>(0, cell) = score;
    obj.at<float>(0, cell) = score;

    outputs = {cls, obj, bbox, kps};
    names = {"cls_8", "obj_8", "bbox_8", "kps_8"};
  }
};

std::vector<FaceBox> Decode(const YuNetOutputs& out) {
  return DecodeYuNet(out.outputs, out.names, cv::Size(out.input, out.input),
                     cv::Size(out.frame_width, out.frame_height), 0.7f);
}

}  // namespace

TEST(FaceDetectorNms, KeepsNonOverlappingBoxes) {
  const auto boxes = Nms({Box(0, 0, 10, 10, 0.9f),
                          Box(100, 100, 10, 10, 0.8f)},
                         0.3f);
  EXPECT_EQ(boxes.size(), 2u);
}

TEST(FaceDetectorNms, SuppressesOverlappingLowerScoreBox) {
  const auto boxes =
      Nms({Box(0, 0, 10, 10, 0.9f), Box(1, 1, 10, 10, 0.8f)}, 0.3f);
  ASSERT_EQ(boxes.size(), 1u);
  EXPECT_FLOAT_EQ(boxes[0].confidence, 0.9f);
}

TEST(FaceDetectorNms, KeepsOverlapBelowThreshold) {
  const auto boxes =
      Nms({Box(0, 0, 10, 10, 0.9f), Box(1, 1, 10, 10, 0.8f)}, 0.9f);
  EXPECT_EQ(boxes.size(), 2u);
}

TEST(FaceDetectorNms, IdenticalBoxesKeepHighestScore) {
  const auto boxes =
      Nms({Box(0, 0, 10, 10, 0.5f), Box(0, 0, 10, 10, 0.9f)}, 0.3f);
  ASSERT_EQ(boxes.size(), 1u);
  EXPECT_FLOAT_EQ(boxes[0].confidence, 0.9f);
}

TEST(FaceDetectorNms, EmptyInputReturnsEmpty) {
  EXPECT_TRUE(Nms({}, 0.3f).empty());
}

TEST(YuNetDecode, DecodesSingleCandidateWithFrameScaling) {
  YuNetOutputs out;
  const auto boxes = Decode(out);

  ASSERT_EQ(boxes.size(), 1u);
  EXPECT_NEAR(boxes[0].confidence, 0.9f, 1e-4);
  EXPECT_NEAR(boxes[0].rect.x, 221.333f, 1e-2);
  EXPECT_NEAR(boxes[0].rect.y, 166.0f, 1e-2);
  EXPECT_NEAR(boxes[0].rect.width, 213.333f, 1e-2);
  EXPECT_NEAR(boxes[0].rect.height, 160.0f, 1e-2);
}

TEST(YuNetDecode, FiltersLowScoreCandidates) {
  YuNetOutputs out;
  out.score = 0.1f;
  out.cls.at<float>(0, out.cell) = out.score;
  out.obj.at<float>(0, out.cell) = out.score;

  EXPECT_TRUE(Decode(out).empty());
}

TEST(YuNetDecode, MissingBboxHeadReturnsEmpty) {
  YuNetOutputs out;
  out.names = {"cls_8", "obj_8", "kps_8"};
  out.outputs = {out.cls, out.obj, out.kps};

  EXPECT_TRUE(Decode(out).empty());
}

TEST(YuNetDecode, MalformedBboxShapeReturnsEmpty) {
  YuNetOutputs out;
  const int fw = out.input / out.stride;
  const int fh = out.input / out.stride;
  const int n = fw * fh;
  out.bbox = cv::Mat::zeros(1, n * 3, CV_32F);
  out.outputs = {out.cls, out.obj, out.bbox, out.kps};

  EXPECT_TRUE(Decode(out).empty());
}
