#include <gtest/gtest.h>

#include <cstdint>

#include "ffmpeg/Pacer.h"
#include "support/fake_clock.h"

using streamsight::ffmpeg::Pacer;
using streamsight::test::FakeClock;

namespace {

struct ClockedPacer {
  FakeClock clock;
  Pacer pacer;

  ClockedPacer()
      : pacer([this]() { return clock.NowUs(); },
              [this](int64_t us) { clock.SleepUs(us); }) {}
};

}  // namespace

TEST(Pacer, DisabledWhenFpsIsNotPositive) {
  ClockedPacer cp;
  cp.pacer.SetFps(0);

  EXPECT_FALSE(cp.pacer.Enabled());
  cp.pacer.Wait();
  EXPECT_EQ(cp.clock.NowUs(), 0);

  cp.pacer.SetFps(-1);
  EXPECT_FALSE(cp.pacer.Enabled());
  cp.pacer.Wait();
  EXPECT_EQ(cp.clock.NowUs(), 0);
}

TEST(Pacer, EnabledForPositiveFps) {
  ClockedPacer cp;
  cp.pacer.SetFps(30);
  EXPECT_TRUE(cp.pacer.Enabled());
}

TEST(Pacer, FirstWaitDoesNotSleep) {
  ClockedPacer cp;
  cp.pacer.SetFps(10);

  cp.pacer.Wait();
  EXPECT_EQ(cp.clock.NowUs(), 0);
}

TEST(Pacer, WaitsOneIntervalPerFrame) {
  ClockedPacer cp;
  cp.pacer.SetFps(10);

  cp.pacer.Wait();
  cp.pacer.Wait();
  EXPECT_EQ(cp.clock.NowUs(), 100000);

  cp.pacer.Wait();
  EXPECT_EQ(cp.clock.NowUs(), 200000);
}

TEST(Pacer, ResyncsAfterFallingBehind) {
  ClockedPacer cp;
  cp.pacer.SetFps(10);

  cp.pacer.Wait();
  cp.clock.AdvanceUs(350000);
  cp.pacer.Wait();
  EXPECT_EQ(cp.clock.NowUs(), 350000);

  cp.pacer.Wait();
  EXPECT_EQ(cp.clock.NowUs(), 450000);
}

TEST(Pacer, ResetRestartsSchedule) {
  ClockedPacer cp;
  cp.pacer.SetFps(10);

  cp.pacer.Wait();
  cp.pacer.Wait();
  EXPECT_EQ(cp.clock.NowUs(), 100000);

  cp.pacer.Reset();
  cp.pacer.Wait();
  EXPECT_EQ(cp.clock.NowUs(), 100000);

  cp.pacer.Wait();
  EXPECT_EQ(cp.clock.NowUs(), 200000);
}
