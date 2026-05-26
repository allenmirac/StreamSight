// tests/test_api_server.cpp
// Structural tests for StreamApiServer: session lifecycle, result/event APIs,
// and nullptr safety for face DB/recognizer.
// Does NOT start the HTTP server, so no network activity.

#include "../src/api/StreamApiServer.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;
#define CHECK(cond) do { \
    if (cond) { tests_passed++; } \
    else { std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; tests_failed++; } \
} while(0)

int main() {
    // Test 1: create and list sessions
    {
        api::StreamApiServer server(19999);
        ffmpeg::StreamSessionConfig cfg;
        cfg.input_url = "test.mp4";
        cfg.enable_ai = false;

        std::string id1 = server.CreateSession(cfg);
        CHECK(!id1.empty());
        CHECK(server.ListSessions().size() == 1);

        std::string id2 = server.CreateSession(cfg);
        CHECK(!id2.empty());
        CHECK(id1 != id2);
        CHECK(server.ListSessions().size() == 2);
    }

    // Test 2: remove session
    {
        api::StreamApiServer server(19998);
        ffmpeg::StreamSessionConfig cfg;
        cfg.enable_ai = false;

        std::string id = server.CreateSession(cfg);
        CHECK(server.ListSessions().size() == 1);
        CHECK(server.RemoveSession(id));
        CHECK(server.ListSessions().size() == 0);
        CHECK(!server.RemoveSession("nonexistent"));
    }

    // Test 3: get session status
    {
        api::StreamApiServer server(19997);
        ffmpeg::StreamSessionConfig cfg;
        cfg.enable_ai = false;

        std::string id = server.CreateSession(cfg);
        auto status = server.GetSessionStatus(id);
        CHECK(status.stream_id == id);
        CHECK(!status.running);
    }

    // Test 4: RegisterSession with externally-created session
    {
        api::StreamApiServer server(19993);
        ffmpeg::StreamSessionConfig cfg;
        cfg.input_url = "external.mp4";
        cfg.enable_ai = false;
        auto session = std::make_shared<ffmpeg::StreamSession>(cfg);
        std::string id = server.RegisterSession(session);
        CHECK(!id.empty());
        CHECK(server.ListSessions().size() == 1);
        auto retrieved = server.GetSession(id);
        CHECK(retrieved == session);
    }

    // Test 5: UpdateResult and AddEvent
    {
        api::StreamApiServer server(19996);
        ai::AnalysisResult r;
        r.frame_id = 42;
        r.timestamp_ms = 1000;
        server.UpdateResult(r);
        server.AddEvent(r);
    }

    // Test 6: nullptr db/recog is safe
    {
        api::StreamApiServer server(19995, nullptr, nullptr);
    }

    // Test 7: GetSession for nonexistent ID returns nullptr
    {
        api::StreamApiServer server(19994);
        CHECK(server.GetSession("nonexistent") == nullptr);
    }

    std::cout << "StreamApiServer: " << tests_passed << "/" << (tests_passed + tests_failed) << " passed" << std::endl;
    return tests_failed;
}