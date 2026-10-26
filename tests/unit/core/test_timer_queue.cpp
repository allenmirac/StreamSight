#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "net/Timer.h"

using streamsight::net::TimerQueue;

TEST(TimerQueue, OneShotTimerFiresOnce) {
  TimerQueue queue;
  int fired = 0;
  queue.AddTimer(
      [&]() {
        ++fired;
        return false;
      },
      1);

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  queue.HandleTimerEvent();
  EXPECT_EQ(fired, 1);

  queue.HandleTimerEvent();
  EXPECT_EQ(fired, 1);
}

TEST(TimerQueue, RepeatingTimerRunsUntilCallbackReturnsFalse) {
  TimerQueue queue;
  int fired = 0;
  queue.AddTimer(
      [&]() {
        ++fired;
        return fired < 3;
      },
      1);

  for (int i = 0; i < 10 && fired < 3; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    queue.HandleTimerEvent();
  }
  EXPECT_EQ(fired, 3);
}

TEST(TimerQueue, RemoveTimerPreventsCallback) {
  TimerQueue queue;
  int fired = 0;
  const auto id = queue.AddTimer(
      [&]() {
        ++fired;
        return false;
      },
      1);
  queue.RemoveTimer(id);

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  queue.HandleTimerEvent();
  EXPECT_EQ(fired, 0);
}

TEST(TimerQueue, ReportsTimeRemaining) {
  TimerQueue queue;
  EXPECT_EQ(queue.GetTimeRemaining(), -1);

  queue.AddTimer([]() { return false; }, 100);
  const int64_t remaining = queue.GetTimeRemaining();
  EXPECT_GE(remaining, 0);
  EXPECT_LE(remaining, 100);
}
