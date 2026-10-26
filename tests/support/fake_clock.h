#pragma once

#include <cstdint>

namespace streamsight::test {

class FakeClock {
 public:
  int64_t NowUs() const { return now_us_; }
  void SleepUs(int64_t us) { now_us_ += us; }
  void AdvanceUs(int64_t us) { now_us_ += us; }

 private:
  int64_t now_us_ = 0;
};

}  // namespace streamsight::test
