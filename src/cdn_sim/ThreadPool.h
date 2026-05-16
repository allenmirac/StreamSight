#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace cdn_sim {

class ThreadPool {
public:
	explicit ThreadPool(size_t num_threads, size_t max_queue_size = 0)
		: stop_(false), active_workers_(0), max_queue_size_(max_queue_size) {
		for (size_t i = 0; i < num_threads; ++i) {
			workers_.emplace_back([this]() {
				for (;;) {
					std::function<void()> task;
					{
						std::unique_lock<std::mutex> lk(mu_);
						cv_.wait(lk, [this]() {
							return stop_ || !tasks_.empty();
						});
						if (stop_ && tasks_.empty()) return;
						task = std::move(tasks_.front());
						tasks_.pop();
						++active_workers_;
					}

					try {
						task();
					} catch (...) {
					}

					{
						std::lock_guard<std::mutex> lk(mu_);
						--active_workers_;
					}
				}
			});
		}
	}

	~ThreadPool() {
		Shutdown();
	}

	bool Submit(std::function<void()> fn) {
		{
			std::lock_guard<std::mutex> lk(mu_);
			if (stop_) return false;
			if (max_queue_size_ > 0 && tasks_.size() >= max_queue_size_) return false;
			tasks_.push(std::move(fn));
		}
		cv_.notify_one();
		return true;
	}

	// Alias for Submit — returns false when queue is full or pool stopped.
	bool TrySubmit(std::function<void()> fn) {
		return Submit(std::move(fn));
	}

	bool IsFull() const {
		std::lock_guard<std::mutex> lk(mu_);
		return max_queue_size_ > 0 && tasks_.size() >= max_queue_size_;
	}

	void Shutdown() {
		{
			std::lock_guard<std::mutex> lk(mu_);
			if (stop_) return;
			stop_ = true;
		}
		cv_.notify_all();
		for (auto& t : workers_) {
			if (t.joinable()) t.join();
		}
	}

	size_t PendingTasks() const {
		std::lock_guard<std::mutex> lk(mu_);
		return tasks_.size();
	}

	size_t ActiveWorkers() const {
		std::lock_guard<std::mutex> lk(mu_);
		return active_workers_;
	}

	size_t Size() const {
		return workers_.size();
	}

private:
	mutable std::mutex mu_;
	std::condition_variable cv_;
	std::queue<std::function<void()>> tasks_;
	std::vector<std::thread> workers_;
	bool stop_;
	size_t active_workers_;
	size_t max_queue_size_;
};

} // namespace cdn_sim
