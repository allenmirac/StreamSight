#include <gtest/gtest.h>
#include <unistd.h>

#include <fstream>
#include <iterator>
#include <string>

#include "observe/LatencyTracer.h"

using streamsight::observe::LatencyEvent;
using streamsight::observe::LatencyScope;
using streamsight::observe::LatencyTracer;

class LatencyTracerTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    path_ = ::testing::TempDir() + "/streamsight_latency_test_" +
            std::to_string(::getpid()) + ".jsonl";
    ::unlink(path_.c_str());
    LatencyTracer::Instance().SetLogPath(path_);
    LatencyTracer::Instance().Enable(true);
  }

  static void TearDownTestSuite() {
    LatencyTracer::Instance().Flush();
    LatencyTracer::Instance().Enable(false);
    ::unlink(path_.c_str());
  }

  void SetUp() override {
    LatencyTracer::Instance().Reset();
    LatencyTracer::Instance().SetBufferSize(10000);
  }

  static std::string path_;
};

std::string LatencyTracerTest::path_;

TEST_F(LatencyTracerTest, StatisticsTrackCountMinMaxTotal) {
  auto& tracer = LatencyTracer::Instance();
  for (int64_t duration : {10, 20, 30}) {
    LatencyEvent event;
    event.module = "lat_stats";
    event.stage = "s";
    event.duration_us = duration;
    tracer.LogEvent(event);
  }

  const auto stats = tracer.GetStats();
  const auto it = stats.find("lat_stats.s");
  ASSERT_NE(it, stats.end());
  EXPECT_EQ(it->second.count, 3);
  EXPECT_EQ(it->second.total_us, 60);
  EXPECT_EQ(it->second.min_us, 10);
  EXPECT_EQ(it->second.max_us, 30);
}

TEST_F(LatencyTracerTest, RecentEventsRespectLimitAndOrder) {
  auto& tracer = LatencyTracer::Instance();
  for (int64_t duration : {10, 20, 30}) {
    LatencyEvent event;
    event.module = "lat_recent";
    event.stage = "s";
    event.duration_us = duration;
    tracer.LogEvent(event);
  }

  const auto events = tracer.GetRecentEvents(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].duration_us, 20);
  EXPECT_EQ(events[1].duration_us, 30);
}

TEST_F(LatencyTracerTest, BufferSizeTrimsOldEvents) {
  auto& tracer = LatencyTracer::Instance();
  tracer.SetBufferSize(2);

  for (int i = 0; i < 3; ++i) {
    LatencyEvent event;
    event.module = "lat_trim";
    event.stage = "s";
    event.duration_us = i;
    tracer.LogEvent(event);
  }
  EXPECT_EQ(tracer.GetRecentEvents(10).size(), 2u);
}

TEST_F(LatencyTracerTest, ResetClearsEventsAndStats) {
  auto& tracer = LatencyTracer::Instance();
  LatencyEvent event;
  event.module = "lat_reset";
  event.stage = "s";
  event.duration_us = 5;
  tracer.LogEvent(event);

  tracer.Reset();
  EXPECT_TRUE(tracer.GetRecentEvents(10).empty());
  EXPECT_TRUE(tracer.GetStats().empty());
}

TEST_F(LatencyTracerTest, ScopeRecordsDuration) {
  {
    LatencyScope scope("lat_scope", "work");
  }

  const auto stats = LatencyTracer::Instance().GetStats();
  const auto it = stats.find("lat_scope.work");
  ASSERT_NE(it, stats.end());
  EXPECT_EQ(it->second.count, 1);
  EXPECT_GE(it->second.total_us, 0);
}

TEST_F(LatencyTracerTest, DurationsAreCappedPerStage) {
  auto& tracer = LatencyTracer::Instance();
  for (int i = 0; i < 1001; ++i) {
    LatencyEvent event;
    event.module = "lat_cap";
    event.stage = "s";
    event.duration_us = i;
    tracer.LogEvent(event);
  }

  const auto stats = tracer.GetStats();
  const auto it = stats.find("lat_cap.s");
  ASSERT_NE(it, stats.end());
  EXPECT_EQ(it->second.durations.size(), 1000u);
}

TEST_F(LatencyTracerTest, FlushWritesValidJsonLines) {
  auto& tracer = LatencyTracer::Instance();
  LatencyEvent event;
  event.module = "lat_file";
  event.stage = "s";
  event.event = "scope";
  event.duration_us = 1;
  tracer.LogEvent(event);
  tracer.Flush();

  std::ifstream input(path_);
  ASSERT_TRUE(input.is_open());
  std::string line;
  bool found = false;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    EXPECT_EQ(line.front(), '{');
    EXPECT_EQ(line.back(), '}');
    if (line.find(R"("module":"lat_file")") != std::string::npos) {
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(LatencyTracerTest, EscapesJsonInModuleName) {
  auto& tracer = LatencyTracer::Instance();
  LatencyEvent event;
  event.module = "lat\"x";
  event.stage = "s";
  event.duration_us = 1;
  tracer.LogEvent(event);
  tracer.Flush();

  std::ifstream input(path_);
  ASSERT_TRUE(input.is_open());
  const std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
  EXPECT_NE(content.find(R"("module":"lat\"x")"), std::string::npos);
}
