// tests/test_smoke_driver.cpp
// Unified test runner — calls each test suite in sequence.
// Compile with -DTEST_SMOKE_BUILD to disable per-file main().

#include <iostream>

// Forward declarations from individual test files (defined when
// TEST_SMOKE_BUILD is set, replacing their stand-alone main()).
int run_event_bus_tests();
int run_effect_factory_tests();
int run_stream_session_tests();
int run_api_server_tests();

int main() {
    int failed = 0;
    failed += run_event_bus_tests();
    failed += run_effect_factory_tests();
    failed += run_stream_session_tests();
    failed += run_api_server_tests();
    std::cout << "test_smoke: " << (failed == 0 ? "ALL PASSED" : "SOME FAILED") << std::endl;
    return failed;
}