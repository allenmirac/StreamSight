#ifndef OBSERVE_EVENT_BUS_H
#define OBSERVE_EVENT_BUS_H

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace streamsight {

template <typename EventType>
class EventBus {
public:
    using Subscriber = std::function<void(const EventType&)>;
    using Handle = size_t;

    Handle Subscribe(Subscriber fn) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        Handle h = next_handle_++;
        subscribers_.push_back({h, std::move(fn)});
        return h;
    }

    void Unsubscribe(Handle handle) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        subscribers_.erase(
            std::remove_if(subscribers_.begin(), subscribers_.end(),
                           [handle](const Entry& entry) {
                               return entry.handle == handle;
                           }),
            subscribers_.end());
    }

    void Publish(const EventType& event) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (auto& entry : subscribers_) {
            entry.fn(event);
        }
    }

    size_t SubscriberCount() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return subscribers_.size();
    }

private:
    struct Entry {
        Handle     handle;
        Subscriber fn;
    };

    mutable std::recursive_mutex mutex_;
    std::vector<Entry>  subscribers_;
    // Handle 0 is reserved and never issued.
    Handle              next_handle_ = 1;
};

}  // namespace streamsight

#endif  // OBSERVE_EVENT_BUS_H