#include "Timer.h"
#include <iostream>
#include <vector>

using namespace xop;
using namespace std;
using namespace std::chrono;

TimerId TimerQueue::AddTimer(const TimerEvent& event, uint32_t ms)
{
	std::lock_guard<std::mutex> locker(mutex_);

	int64_t timeout = GetTimeNow();
	TimerId timer_id = ++last_timer_id_;

	auto timer = make_shared<Timer>(event, ms);
	timer->SetNextTimeout(timeout);
	timers_.emplace(timer_id, timer);
	events_.emplace(std::pair<int64_t, TimerId>(timeout + ms, timer_id), std::move(timer));
	return timer_id;
}

void TimerQueue::RemoveTimer(TimerId timerId)
{
	std::lock_guard<std::mutex> locker(mutex_);

	auto iter = timers_.find(timerId);
	if (iter != timers_.end()) {
		int64_t timeout = iter->second->getNextTimeout();
		events_.erase(std::pair<int64_t, TimerId>(timeout, timerId));
		timers_.erase(timerId);
	}
}

int64_t TimerQueue::GetTimeNow()
{
	auto time_point = steady_clock::now();
	return duration_cast<milliseconds>(time_point.time_since_epoch()).count();
}

int64_t TimerQueue::GetTimeRemaining()
{
	std::lock_guard<std::mutex> locker(mutex_);

	if (timers_.empty()) {
		return -1;
	}

	int64_t msec = events_.begin()->first.first - GetTimeNow();
	if (msec < 0) {
		msec = 0;
	}

	return msec;
}

void TimerQueue::HandleTimerEvent()
{
	if (timers_.empty()) {
		return;
	}

	// Collect expired timer callbacks under lock, then invoke them
	// outside the lock to avoid deadlock if a callback calls AddTimer/RemoveTimer.
	std::vector<std::pair<TimerId, std::shared_ptr<Timer>>> expired;

	{
		std::lock_guard<std::mutex> locker(mutex_);
		int64_t timePoint = GetTimeNow();
		while (!events_.empty() && events_.begin()->first.first <= timePoint)
		{
			TimerId timerId = events_.begin()->first.second;
			auto timerPtr = events_.begin()->second;
			events_.erase(events_.begin());
			expired.emplace_back(timerId, timerPtr);
		}
	}

	// Invoke callbacks outside lock
	std::vector<std::pair<TimerId, std::shared_ptr<Timer>>> to_reschedule;

	for (auto& item : expired) {
		TimerId timerId = item.first;
		auto& timerPtr = item.second;
		bool repeat = timerPtr->event_callback_();
		if (repeat) {
			to_reschedule.push_back(std::move(item));
		} else {
			std::lock_guard<std::mutex> locker(mutex_);
			timers_.erase(timerId);
		}
	}

	// Reschedule repeating timers
	if (!to_reschedule.empty()) {
		std::lock_guard<std::mutex> locker(mutex_);
		int64_t timePoint = GetTimeNow();
		for (auto& item : to_reschedule) {
			TimerId timerId = item.first;
			auto& timerPtr = item.second;
			timerPtr->SetNextTimeout(timePoint);
			events_.emplace(std::pair<int64_t, TimerId>(timerPtr->getNextTimeout(), timerId), timerPtr);
		}
	}
}
