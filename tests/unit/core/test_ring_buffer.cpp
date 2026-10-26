#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "net/RingBuffer.h"

namespace {

struct Item {
  int value = 0;
  int64_t ts = 0;
  bool key = false;

  int64_t Timestamp() const { return ts; }
  bool IsKey() const { return key; }
};

using streamsight::net::RingBuffer;

}  // namespace

TEST(RingBuffer, PopFailsWhenEmpty) {
  RingBuffer<int> rb(2);
  int value = 0;
  EXPECT_FALSE(rb.Pop(value));
  EXPECT_TRUE(rb.IsEmpty());
}

TEST(RingBuffer, PushFailsWhenFull) {
  RingBuffer<int> rb(2);
  EXPECT_TRUE(rb.Push(1));
  EXPECT_TRUE(rb.Push(2));
  EXPECT_FALSE(rb.Push(3));
  EXPECT_TRUE(rb.IsFull());
}

TEST(RingBuffer, PreservesFifoOrder) {
  RingBuffer<int> rb(3);
  EXPECT_TRUE(rb.Push(1));
  EXPECT_TRUE(rb.Push(2));

  int value = 0;
  ASSERT_TRUE(rb.Pop(value));
  EXPECT_EQ(value, 1);
  ASSERT_TRUE(rb.Pop(value));
  EXPECT_EQ(value, 2);
  EXPECT_FALSE(rb.Pop(value));
}

TEST(RingBuffer, PushOverwriteDropsOldest) {
  RingBuffer<int> rb(2);
  EXPECT_TRUE(rb.Push(1));
  EXPECT_TRUE(rb.Push(2));
  EXPECT_TRUE(rb.PushOverwrite(3));

  int value = 0;
  ASSERT_TRUE(rb.Pop(value));
  EXPECT_EQ(value, 2);
  ASSERT_TRUE(rb.Pop(value));
  EXPECT_EQ(value, 3);
  EXPECT_EQ(rb.Size(), 0);
}

TEST(RingBuffer, PushOrDropAcceptsWhenNotFull) {
  RingBuffer<Item> rb(2);
  const auto status =
      rb.PushOrDrop(Item{1, 100, false}, 200, 500, [](const Item& i) {
        return i.Timestamp();
      });
  EXPECT_EQ(status, RingBuffer<Item>::DropStatus::Accepted);
  EXPECT_EQ(rb.Size(), 1);
}

TEST(RingBuffer, PushOrDropDropsOldestWhenStale) {
  RingBuffer<Item> rb(1);
  EXPECT_TRUE(rb.Push(Item{1, 0, false}));

  const auto status =
      rb.PushOrDrop(Item{2, 100, false}, 100000, 50000,
                    [](const Item& i) { return i.Timestamp(); });
  EXPECT_EQ(status, RingBuffer<Item>::DropStatus::OldDropped);

  Item out;
  ASSERT_TRUE(rb.Pop(out));
  EXPECT_EQ(out.value, 2);
}

TEST(RingBuffer, PushOrDropDropsNewWhenOldestFresh) {
  RingBuffer<Item> rb(1);
  EXPECT_TRUE(rb.Push(Item{1, 90000, false}));

  const auto status =
      rb.PushOrDrop(Item{2, 95000, false}, 100000, 50000,
                    [](const Item& i) { return i.Timestamp(); });
  EXPECT_EQ(status, RingBuffer<Item>::DropStatus::NewDropped);

  Item out;
  ASSERT_TRUE(rb.Pop(out));
  EXPECT_EQ(out.value, 1);
}

TEST(RingBuffer, PruneStaleRemovesOldEntries) {
  RingBuffer<Item> rb(5);
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(rb.Push(Item{i, i * 10, false}));
  }

  const int pruned =
      rb.PruneStale(25, [](const Item& i) { return i.Timestamp(); });
  EXPECT_EQ(pruned, 3);
  EXPECT_EQ(rb.Size(), 2);

  Item out;
  ASSERT_TRUE(rb.Pop(out));
  EXPECT_EQ(out.value, 3);
  ASSERT_TRUE(rb.Pop(out));
  EXPECT_EQ(out.value, 4);
}

TEST(RingBuffer, PruneStaleStopsAtFirstFreshEntry) {
  RingBuffer<Item> rb(5);
  rb.Push(Item{0, 0, false});
  rb.Push(Item{1, 100, false});
  rb.Push(Item{2, 200, false});

  const int pruned =
      rb.PruneStale(50, [](const Item& i) { return i.Timestamp(); });
  EXPECT_EQ(pruned, 1);
  EXPECT_EQ(rb.Size(), 2);
}

TEST(RingBuffer, PruneStaleMovesKeyframesToBack) {
  RingBuffer<Item> rb(5);
  rb.Push(Item{0, 0, false});
  rb.Push(Item{1, 10, true});
  rb.Push(Item{2, 20, false});

  const int pruned = rb.PruneStale(
      15, [](const Item& i) { return i.Timestamp(); },
      [](const Item& i) { return i.IsKey(); });
  EXPECT_EQ(pruned, 1);
  EXPECT_EQ(rb.Size(), 2);

  Item out;
  ASSERT_TRUE(rb.Pop(out));
  EXPECT_EQ(out.value, 2);
  ASSERT_TRUE(rb.Pop(out));
  EXPECT_EQ(out.value, 1);
}

TEST(RingBuffer, PeekOldestDoesNotConsume) {
  RingBuffer<int> rb(3);
  rb.Push(7);

  int value = 0;
  ASSERT_TRUE(rb.PeekOldest(value));
  EXPECT_EQ(value, 7);
  EXPECT_EQ(rb.Size(), 1);

  ASSERT_TRUE(rb.Pop(value));
  EXPECT_EQ(value, 7);
}

TEST(RingBuffer, WrapsAroundCapacity) {
  RingBuffer<int> rb(3);
  rb.Push(1);
  rb.Push(2);
  rb.Push(3);

  int value = 0;
  ASSERT_TRUE(rb.Pop(value));
  EXPECT_EQ(value, 1);

  EXPECT_TRUE(rb.Push(4));
  ASSERT_TRUE(rb.Pop(value));
  EXPECT_EQ(value, 2);

  EXPECT_TRUE(rb.Push(5));

  ASSERT_TRUE(rb.Pop(value));
  EXPECT_EQ(value, 3);
  ASSERT_TRUE(rb.Pop(value));
  EXPECT_EQ(value, 4);
  ASSERT_TRUE(rb.Pop(value));
  EXPECT_EQ(value, 5);
}

TEST(RingBuffer, SingleProducerSingleConsumerPreservesOrder) {
  RingBuffer<int> rb(64);
  constexpr int kCount = 200000;

  std::thread producer([&]() {
    for (int i = 0; i < kCount;) {
      if (rb.Push(i)) ++i;
    }
  });

  std::vector<int> values;
  values.reserve(kCount);
  while (values.size() < static_cast<size_t>(kCount)) {
    int value = 0;
    if (rb.Pop(value)) values.push_back(value);
  }
  producer.join();

  ASSERT_EQ(values.size(), static_cast<size_t>(kCount));
  for (int i = 0; i < kCount; ++i) {
    ASSERT_EQ(values[static_cast<size_t>(i)], i);
  }
}
