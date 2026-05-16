#ifndef XOP_RING_BUFFER_H
#define XOP_RING_BUFFER_H

#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>
#include <iostream>

namespace xop
{

template <typename T>
class RingBuffer
{
public:
	enum class DropStatus {
		Accepted,
		OldDropped,
		NewDropped
	};

	RingBuffer(int capacity = 60)
		: capacity_(capacity)
		, num_datas_(0)
		, buffer_(capacity)
	{ }

	virtual ~RingBuffer() {	}

	bool Push(const T& data)
	{
		return PushData(data);
	}

	bool Push(T&& data)
	{
		return PushData(std::move(data));
	}

	bool Pop(T& data)
	{
		if (num_datas_ > 0) {
			int pos = get_pos_.load(std::memory_order_acquire);
			data = std::move(buffer_[pos]);
			Advance(get_pos_);
			num_datas_--;
			return true;
		}

		return false;
	}

	// Push even when full, overwriting the oldest entry.
	// Thread-safe for single-producer single-consumer use.
	bool PushOverwrite(T&& data)
	{
		std::lock_guard<std::mutex> lock(overwrite_mutex_);
		if (num_datas_ >= capacity_) {
			Advance(get_pos_);
			num_datas_--;
		}
		int pos = put_pos_.load(std::memory_order_relaxed);
		buffer_[pos] = std::move(data);
		Advance(put_pos_);
		num_datas_++;
		return true;
	}

	// Peek oldest entry without popping. Returns false if empty.
	bool PeekOldest(T& data)
	{
		std::lock_guard<std::mutex> lock(overwrite_mutex_);
		if (num_datas_ <= 0) return false;
		int pos = get_pos_.load(std::memory_order_relaxed);
		data = buffer_[pos];
		return true;
	}

	// Push with age-based dropping. When full, checks oldest entry's
	// timestamp via peek_timestamp(). If oldest is older than max_age_us,
	// drops it and inserts the new frame. Otherwise the new frame is discarded.
	// Returns the DropStatus indicating what happened.
	// Caller provides a function that extracts timestamp (in microseconds) from T.
	DropStatus PushOrDrop(T&& data, int64_t now_us, int64_t max_age_us,
	                      std::function<int64_t(const T&)> peek_timestamp)
	{
		std::lock_guard<std::mutex> lock(overwrite_mutex_);
		if (num_datas_ < capacity_) {
			int pos = put_pos_.load(std::memory_order_relaxed);
			buffer_[pos] = std::move(data);
			Advance(put_pos_);
			num_datas_++;
			return DropStatus::Accepted;
		}

		// Buffer full — check oldest entry age
		int pos = get_pos_.load(std::memory_order_relaxed);
		int64_t oldest_ts = peek_timestamp(buffer_[pos]);
		if (now_us - oldest_ts > max_age_us) {
			buffer_[pos] = std::move(data);
			Advance(get_pos_);
			Advance(put_pos_);
			return DropStatus::OldDropped;
		}
		return DropStatus::NewDropped;
	}

	bool IsFull()  const
	{
		return ((num_datas_==capacity_) ? true : false);
	}

	bool IsEmpty() const
	{
		return ((num_datas_==0) ? true : false);
	}

	int  Size() const
	{
		return num_datas_;
	}

	int  Capacity() const
	{
		return capacity_;
	}

private:
	template <typename F>
	bool PushData(F&& data)
	{
		if (num_datas_ < capacity_) {
			int pos = put_pos_.load(std::memory_order_relaxed);
			buffer_[pos] = std::forward<F>(data);
			Advance(put_pos_);
			num_datas_++;
			return true;
		}

		return false;
	}

	void Advance(std::atomic<int>& pos)
	{
		int cur = pos.load(std::memory_order_relaxed);
		int next = (cur + 1) % capacity_;
		pos.store(next, std::memory_order_release);
	}

	int capacity_ = 0;
	std::atomic<int> put_pos_{0};
	std::atomic<int> get_pos_{0};

	std::atomic_int num_datas_;
	std::vector<T> buffer_;
	std::mutex overwrite_mutex_;  // guards PushOverwrite / PushOrDrop / PeekOldest
};

}

#endif
