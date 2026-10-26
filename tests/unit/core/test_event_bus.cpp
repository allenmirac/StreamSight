#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "observe/EventBus.h"

namespace {

struct TestEvent {
  std::string stream_id;
  int64_t frame_id;
  std::string payload;
};

}  // namespace

TEST(EventBus, SubscriberReceivesEventsUntilUnsubscribed) {
  streamsight::observe::EventBus<TestEvent> bus;
  int count = 0;
  auto handle = bus.Subscribe([&](const TestEvent&) { ++count; });

  bus.Publish(TestEvent{"s1", 0, "hello"});
  bus.Publish(TestEvent{"s1", 1, "world"});
  EXPECT_EQ(count, 2);

  bus.Unsubscribe(handle);
  bus.Publish(TestEvent{"s1", 2, "ignored"});
  EXPECT_EQ(count, 2);
}

TEST(EventBus, MultipleSubscribersReceiveEachEvent) {
  streamsight::observe::EventBus<TestEvent> bus;
  int a = 0;
  int b = 0;
  auto h1 = bus.Subscribe([&](const TestEvent&) { ++a; });
  auto h2 = bus.Subscribe([&](const TestEvent&) { ++b; });

  bus.Publish(TestEvent{"s1", 0, ""});
  EXPECT_EQ(a, 1);
  EXPECT_EQ(b, 1);

  bus.Unsubscribe(h1);
  bus.Unsubscribe(h2);
}

TEST(EventBus, ConcurrentPublishIsThreadSafe) {
  streamsight::observe::EventBus<TestEvent> bus;
  std::atomic<int> total{0};
  auto handle = bus.Subscribe([&](const TestEvent&) { total.fetch_add(1); });

  std::thread t1([&]() {
    for (int i = 0; i < 1000; ++i) bus.Publish(TestEvent{"t1", i, ""});
  });
  std::thread t2([&]() {
    for (int i = 0; i < 1000; ++i) bus.Publish(TestEvent{"t2", i, ""});
  });
  t1.join();
  t2.join();

  EXPECT_EQ(total.load(), 2000);
  bus.Unsubscribe(handle);
}
