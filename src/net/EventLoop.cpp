#include "EventLoop.h"
#include "EpollTaskScheduler.h"

using namespace xop;

EventLoop::EventLoop(uint32_t num_threads)
	: index_(1)
	, num_threads_(1)
{
	if (num_threads > 0) {
		num_threads_ = num_threads;
	}

	this->Loop();
}

EventLoop::~EventLoop()
{
	this->Quit();
}

std::shared_ptr<TaskScheduler> EventLoop::GetTaskScheduler()
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() == 1) {
		return task_schedulers_.at(0);
	}
	else {
		auto task_scheduler = task_schedulers_.at(index_);
		index_++;
		if (index_ >= task_schedulers_.size()) {
			index_ = 1;
		}
		return task_scheduler;
	}

	return nullptr;
}

void EventLoop::Loop()
{
	std::lock_guard<std::mutex> locker(mutex_);

	if (!task_schedulers_.empty()) {
		return ;
	}

	for (uint32_t n = 0; n < num_threads_; n++)
	{
#if defined(__linux) || defined(__linux__)
		std::shared_ptr<TaskScheduler> task_scheduler_ptr(new EpollTaskScheduler(n));
#elif defined(WIN32) || defined(_WIN32)
		std::shared_ptr<TaskScheduler> task_scheduler_ptr(new SelectTaskScheduler(n));
#endif
		task_schedulers_.push_back(task_scheduler_ptr);
		std::shared_ptr<std::thread> thread(new std::thread(&TaskScheduler::Start, task_scheduler_ptr.get()));
		threads_.push_back(thread);
	}
}

void EventLoop::Quit()
{
	std::lock_guard<std::mutex> locker(mutex_);

	for (auto iter : task_schedulers_) {
		iter->Stop();
	}

	for (auto iter : threads_) {
		iter->join();
	}

	task_schedulers_.clear();
	threads_.clear();
}

void EventLoop::UpdateChannel(ChannelPtr channel)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		task_schedulers_[0]->UpdateChannel(channel);
	}
}

void EventLoop::RemoveChannel(ChannelPtr& channel)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		task_schedulers_[0]->RemoveChannel(channel);
	}
}

TimerId EventLoop::AddTimer(TimerEvent timerEvent, uint32_t msec)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		return task_schedulers_[0]->AddTimer(timerEvent, msec);
	}
	return 0;
}

void EventLoop::RemoveTimer(TimerId timerId)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		task_schedulers_[0]->RemoveTimer(timerId);
	}
}

bool EventLoop::AddTriggerEvent(TriggerEvent callback)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (task_schedulers_.size() > 0) {
		return task_schedulers_[0]->AddTriggerEvent(callback);
	}
	return false;
}

int64_t EventLoop::GetLoopCount(int scheduler_id) {
	std::lock_guard<std::mutex> locker(mutex_);
	if (scheduler_id >= 0 && (size_t)scheduler_id < task_schedulers_.size())
		return task_schedulers_[scheduler_id]->GetLoopCount();
	return 0;
}

double EventLoop::GetAvgLoopUs(int scheduler_id) {
	std::lock_guard<std::mutex> locker(mutex_);
	if (scheduler_id >= 0 && (size_t)scheduler_id < task_schedulers_.size())
		return task_schedulers_[scheduler_id]->GetAvgLoopUs();
	return 0.0;
}

int EventLoop::GetActiveFdCount(int scheduler_id) {
	std::lock_guard<std::mutex> locker(mutex_);
	if (scheduler_id >= 0 && (size_t)scheduler_id < task_schedulers_.size()) {
		auto* ep = dynamic_cast<EpollTaskScheduler*>(task_schedulers_[scheduler_id].get());
		if (ep) return ep->GetLastActiveFdCount();
	}
	return 0;
}
