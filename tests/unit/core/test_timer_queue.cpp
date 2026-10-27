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

TEST(TimerQueue, MultipleTimersFireInDeadlineOrder) {
  TimerQueue queue;
  int first_fired = 0;
  int second_fired = 0;
  queue.AddTimer(
      [&]() {
        ++first_fired;
        return false;
      },
      10);
  queue.AddTimer(
      [&]() {
        ++second_fired;
        return false;
      },
      100);

  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  queue.HandleTimerEvent();
  EXPECT_EQ(first_fired, 1);
  EXPECT_EQ(second_fired, 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  queue.HandleTimerEvent();
  EXPECT_EQ(first_fired, 1);
  EXPECT_EQ(second_fired, 1);
}

TEST(TimerQueue, TimeRemainingDecreases) {
  TimerQueue queue;
  queue.AddTimer([]() { return false; }, 200);

  const int64_t before = queue.GetTimeRemaining();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  const int64_t after = queue.GetTimeRemaining();

  EXPECT_GE(before, 0);
  EXPECT_LT(after, before);
}

TEST(TimerQueue, ReportsTimeRemaining) {
  TimerQueue queue;
  EXPECT_EQ(queue.GetTimeRemaining(), -1);

  queue.AddTimer([]() { return false; }, 100);
  const int64_t remaining = queue.GetTimeRemaining();
  EXPECT_GE(remaining, 0);
  EXPECT_LE(remaining, 100);
}
