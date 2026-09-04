
#ifndef XOP_EVENT_LOOP_H
#define XOP_EVENT_LOOP_H

#include <memory>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <queue>
#include <thread>
#include <mutex>

#include "SelectTaskScheduler.h"
#include "EpollTaskScheduler.h"
#include "Pipe.h"
#include "Timer.h"
#include "RingBuffer.h"

namespace xop
{

class EventLoop 
{
public:
	EventLoop(const EventLoop&) = delete;
	EventLoop &operator = (const EventLoop&) = delete; 
	EventLoop(uint32_t num_threads =1);  //std::thread::hardware_concurrency()
	virtual ~EventLoop();

	std::shared_ptr<TaskScheduler> GetTaskScheduler();

	bool AddTriggerEvent(TriggerEvent callback);
	TimerId AddTimer(TimerEvent timerEvent, uint32_t msec);
	void RemoveTimer(TimerId timerId);	
	void UpdateChannel(ChannelPtr channel);
	void RemoveChannel(ChannelPtr& channel);
	
	void Loop();
	void Quit();

	int64_t GetLoopCount(int scheduler_id = 0);
	double  GetAvgLoopUs(int scheduler_id = 0);
	int     GetActiveFdCount(int scheduler_id = 0);

private:
	std::mutex mutex_;
	uint32_t num_threads_ = 1;
	uint32_t index_ = 1;
	std::vector<std::shared_ptr<TaskScheduler>> task_schedulers_;
	std::vector<std::shared_ptr<std::thread>> threads_;
};

}

#endif

