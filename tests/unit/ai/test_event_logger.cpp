#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ai/EventLogger.h"
#include "support/test_temp_dir.h"

using streamsight::ai::AnalysisResult;
using streamsight::ai::EventLogger;
using streamsight::ai::FaceResult;
using streamsight::test::TempDir;

namespace {

AnalysisResult OneFace(int frame_id, const std::string& name = "Alice") {
  AnalysisResult result;
  result.timestamp_ms = 1234;
  result.frame_id = frame_id;

  FaceResult face;
  face.box = cv::Rect(10, 20, 30, 40);
  face.confidence = 0.95f;
  face.name = name;
  face.similarity = 0.8f;
  face.recognized = true;
  result.faces.push_back(face);
  return result;
}

size_t CountLines(const std::string& path) {
  std::ifstream input(path);
  size_t count = 0;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) ++count;
  }
  return count;
}

}  // namespace

TEST(EventLogger, WritesOneJsonLine) {
  TempDir dir;
  const std::string path = dir.Path("events.jsonl");

  EventLogger logger(path, 1);
  ASSERT_TRUE(logger.Open());
  logger.Log(OneFace(7));
  logger.Close();

  std::ifstream input(path);
  std::string line;
  ASSERT_TRUE(std::getline(input, line));
  EXPECT_EQ(line.front(), '{');
  EXPECT_EQ(line.back(), '}');
  EXPECT_NE(line.find(R"("frame":7)"), std::string::npos);
  EXPECT_NE(line.find(R"("name":"Alice")"), std::string::npos);
}

TEST(EventLogger, MinFacesFiltersEvents) {
  TempDir dir;
  const std::string path = dir.Path("events.jsonl");

  EventLogger logger(path, 2);
  ASSERT_TRUE(logger.Open());
  logger.Log(OneFace(1));
  logger.Close();

  EXPECT_EQ(CountLines(path), 0u);
}

TEST(EventLogger, AppendsAcrossReopen) {
  TempDir dir;
  const std::string path = dir.Path("events.jsonl");

  {
    EventLogger logger(path, 1);
    ASSERT_TRUE(logger.Open());
    logger.Log(OneFace(1));
  }
  {
    EventLogger logger(path, 1);
    ASSERT_TRUE(logger.Open());
    logger.Log(OneFace(2));
  }

  EXPECT_EQ(CountLines(path), 2u);
}

TEST(EventLogger, UnwritablePathFailsSafely) {
  EventLogger logger("/nonexistent_dir_streamsight/events.jsonl", 1);
  EXPECT_FALSE(logger.Open());
  logger.Log(OneFace(1));
  logger.Close();
}

TEST(EventLogger, ConcurrentLogsDoNotInterleave) {
  TempDir dir;
  const std::string path = dir.Path("events.jsonl");

  EventLogger logger(path, 1);
  ASSERT_TRUE(logger.Open());

  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&logger, t]() {
      for (int i = 0; i < 25; ++i) logger.Log(OneFace(t * 100 + i));
    });
  }
  for (auto& thread : threads) thread.join();
  logger.Close();

  std::ifstream input(path);
  std::string line;
  size_t count = 0;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    EXPECT_EQ(line.front(), '{');
    EXPECT_EQ(line.back(), '}');
    ++count;
  }
  EXPECT_EQ(count, 100u);
}
