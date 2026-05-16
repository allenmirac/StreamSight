// PithyPrint.h
// SRS-inspired throttled debug print — fires at most once per interval.
// Use for periodic backlog/heartbeat logging without spamming stdout.

#ifndef OBSERVE_PITHY_PRINT_H
#define OBSERVE_PITHY_PRINT_H

#include <chrono>
#include <cstdint>

namespace observe {

class PithyPrint {
public:
	explicit PithyPrint(int64_t interval_ms = 3000)
		: interval_us_(interval_ms * 1000)
		, last_log_us_(0)
	{}

	// Returns true at most once per interval_ms. Resets the internal
	// timer each time it fires, so callers should call ShouldLog()
	// unconditionally and only log when it returns true.
	bool ShouldLog() {
		auto now = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		if (now - last_log_us_ >= interval_us_) {
			last_log_us_ = now;
			return true;
		}
		return false;
	}

	// Force-reset the throttle timer (e.g. after an important state change).
	void Reset() {
		last_log_us_ = 0;
	}

private:
	int64_t interval_us_;
	int64_t last_log_us_;
};

} // namespace observe

#endif // OBSERVE_PITHY_PRINT_H
