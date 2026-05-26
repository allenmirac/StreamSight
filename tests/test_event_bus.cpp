// tests/test_event_bus.cpp
#include "../src/observe/EventBus.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

struct TestEvent {
    std::string stream_id;
    int64_t     frame_id;
    std::string payload;
};

static int tests_passed = 0;
static int tests_failed = 0;
#define CHECK(cond) do { \
    if (cond) { tests_passed++; } \
    else { std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; tests_failed++; } \
} while(0)

#ifndef TEST_SMOKE_BUILD
int main() {
#else
int run_event_bus_tests() {
#endif
    // Test 1: single subscriber receives events
    {
        streamsight::EventBus<TestEvent> bus;
        int count = 0;
        auto handle = bus.Subscribe([&](const TestEvent& e) { count++; });
        bus.Publish(TestEvent{"s1", 0, "hello"});
        bus.Publish(TestEvent{"s1", 1, "world"});
        CHECK(count == 2);
        bus.Unsubscribe(handle);
        bus.Publish(TestEvent{"s1", 2, "noone"});
        CHECK(count == 2);  // unsubscribed, shouldn't increment
    }

    // Test 2: multiple subscribers
    {
        streamsight::EventBus<TestEvent> bus;
        int a = 0, b = 0;
        auto h1 = bus.Subscribe([&](const TestEvent&) { a++; });
        auto h2 = bus.Subscribe([&](const TestEvent&) { b++; });
        bus.Publish(TestEvent{"s1", 0, ""});
        CHECK(a == 1);
        CHECK(b == 1);
    }

    // Test 3: thread safety (basic smoke)
    {
        streamsight::EventBus<TestEvent> bus;
        std::atomic<int> total{0};
        auto h = bus.Subscribe([&](const TestEvent&) { total++; });
        std::thread t1([&]() {
            for (int i = 0; i < 1000; i++) bus.Publish(TestEvent{"t", i, ""});
        });
        std::thread t2([&]() {
            for (int i = 0; i < 1000; i++) bus.Publish(TestEvent{"t", i, ""});
        });
        t1.join(); t2.join();
        CHECK(total == 2000);
    }

    std::cout << "EventBus: " << tests_passed << "/" << (tests_passed + tests_failed) << " passed" << std::endl;
    return tests_failed;
}