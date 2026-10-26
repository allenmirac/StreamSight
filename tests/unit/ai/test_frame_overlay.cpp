#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

#include "ai/FrameOverlay.h"

using streamsight::ai::AnalysisResult;
using streamsight::ai::FaceResult;
using streamsight::ai::FrameOverlay;

namespace {

AnalysisResult OneFace(cv::Rect box) {
  AnalysisResult result;
  FaceResult face;
  face.box = box;
  face.confidence = 0.9f;
  face.name = "alice";
  face.similarity = 0.8f;
  face.recognized = true;
  result.faces.push_back(face);
  return result;
}

}  // namespace

TEST(FrameOverlay, DrawsInsideFaceBox) {
  cv::Mat frame(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
  const cv::Mat before = frame.clone();

  FrameOverlay overlay;
  overlay.Draw(frame, OneFace(cv::Rect(20, 20, 30, 30)));

  EXPECT_GT(cv::norm(frame, before, cv::NORM_L1), 0.0);
  const cv::Vec3b after = frame.at<cv::Vec3b>(20, 20);
  const cv::Vec3b prior = before.at<cv::Vec3b>(20, 20);
  EXPECT_TRUE(after[0] != prior[0] || after[1] != prior[1] ||
              after[2] != prior[2]);
}

TEST(FrameOverlay, EmptyResultDrawsNoFaceAnnotations) {
  cv::Mat frame(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
  const cv::Mat before = frame.clone();

  FrameOverlay overlay;
  overlay.Draw(frame, AnalysisResult{});

  // Draw() always renders the "Faces: N" frame-info label in the top-left
  // band (measured: x 11..91, y 11..26 for the default style), so the frame
  // is not byte-identical. With no faces, nothing below that band may change.
  const cv::Rect below_info_label(0, 40, frame.cols, frame.rows - 40);
  EXPECT_DOUBLE_EQ(cv::norm(frame(below_info_label), before(below_info_label),
                            cv::NORM_L1),
                   0.0);
}

TEST(FrameOverlay, OutOfBoundsBoxDoesNotCrash) {
  cv::Mat frame(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));

  FrameOverlay overlay;
  overlay.Draw(frame, OneFace(cv::Rect(-50, -50, 20, 20)));
  overlay.Draw(frame, OneFace(cv::Rect(90, 90, 50, 50)));

  SUCCEED();
}
