#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include "observe/MetricsRegistry.h"

using streamsight::observe::MetricsRegistry;

TEST(MetricsRegistry, GaugesAndCounters) {
  auto& metrics = MetricsRegistry::Instance();
  const std::string scope = "test_metrics_gauges";

  metrics.SetGauge(scope, "g", 1.5);
  EXPECT_DOUBLE_EQ(metrics.GetMetric(scope, "g"), 1.5);

  metrics.IncCounter(scope, "c");
  metrics.IncCounter(scope, "c", 2.5);
  EXPECT_DOUBLE_EQ(metrics.GetMetric(scope, "c"), 3.5);

  const auto snapshot = metrics.Snapshot(scope);
  EXPECT_DOUBLE_EQ(snapshot.at(scope + ".g"), 1.5);
  EXPECT_DOUBLE_EQ(snapshot.at(scope + ".c"), 3.5);
}

TEST(MetricsRegistry, MissingMetricReturnsZero) {
  EXPECT_DOUBLE_EQ(
      MetricsRegistry::Instance().GetMetric("test_metrics_missing", "x"), 0.0);
}

TEST(MetricsRegistry, SnapshotPrefixFilters) {
  auto& metrics = MetricsRegistry::Instance();
  metrics.SetGauge("test_metrics_prefix_a", "x", 1.0);
  metrics.SetGauge("test_metrics_prefix_b", "x", 2.0);

  const auto snapshot = metrics.Snapshot("test_metrics_prefix_a");
  EXPECT_EQ(snapshot.size(), 1u);
  EXPECT_EQ(snapshot.begin()->first, "test_metrics_prefix_a.x");
}

TEST(MetricsRegistry, RemovePrefixOnlyRemovesMatchingKeys) {
  auto& metrics = MetricsRegistry::Instance();
  metrics.SetGauge("test_metrics_remove_a", "x", 1.0);
  metrics.SetGauge("test_metrics_remove_a", "y", 2.0);
  // Shares the textual prefix but not the "scope." boundary.
  metrics.SetGauge("test_metrics_remove_ab", "x", 3.0);
  metrics.SetGauge("test_metrics_remove_b", "x", 4.0);

  EXPECT_EQ(metrics.RemovePrefix("test_metrics_remove_a."), 2u);
  EXPECT_DOUBLE_EQ(metrics.GetMetric("test_metrics_remove_a", "x"), 0.0);
  EXPECT_DOUBLE_EQ(metrics.GetMetric("test_metrics_remove_a", "y"), 0.0);
  EXPECT_DOUBLE_EQ(metrics.GetMetric("test_metrics_remove_ab", "x"), 3.0);
  EXPECT_DOUBLE_EQ(metrics.GetMetric("test_metrics_remove_b", "x"), 4.0);

  EXPECT_EQ(metrics.RemovePrefix("test_metrics_remove_"), 2u);
  EXPECT_TRUE(metrics.Snapshot("test_metrics_remove_").empty());
}

TEST(MetricsRegistry, ConcurrentIncrementsAreSafe) {
  auto& metrics = MetricsRegistry::Instance();
  const std::string scope = "test_metrics_concurrent";

  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&]() {
      for (int i = 0; i < 10000; ++i) metrics.IncCounter(scope, "c");
    });
  }
  for (auto& thread : threads) thread.join();

  EXPECT_DOUBLE_EQ(metrics.GetMetric(scope, "c"), 40000.0);
}
