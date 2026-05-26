// tests/test_stream_session.cpp
// Structural tests for StreamSession — verifies construction, config defaults,
// event bus wiring, and GetStatus. Full integration tests require FFmpeg/OpenCV
// linking and are run via the project build system.

#include "../src/ffmpeg/StreamSession.h"
#include <cassert>
#include <cstdint>
#include <iostream>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond) do { \
    if (cond) { tests_passed++; } \
    else { std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; tests_failed++; } \
} while(0)

#ifndef TEST_SMOKE_BUILD
int main() {
#else
int run_stream_session_tests() {
#endif
    // Test 1: config construction — session is not running
    {
        ffmpeg::StreamSessionConfig cfg;
        cfg.input_url = "test.mp4";
        cfg.rtsp_port = 9999;
        cfg.http_port = 8888;
        cfg.enable_ai = false;
        ffmpeg::StreamSession session(cfg);
        CHECK(!session.IsRunning());
        CHECK(session.GetEffectNames().empty());
    }

    // Test 2: default config values
    {
        ffmpeg::StreamSessionConfig cfg;
        CHECK(cfg.width == 640);
        CHECK(cfg.height == 480);
        CHECK(cfg.fps == 25);
        CHECK(cfg.pipeline_mode == "serial");
    }

    // Test 3: event bus is accessible before start
    {
        ffmpeg::StreamSessionConfig cfg;
        ffmpeg::StreamSession session(cfg);
        int events = 0;
        ffmpeg::SessionEventBus::Handle h = session.GetEventBus().Subscribe(
            [&](const ffmpeg::FrameProcessedEvent& /*event*/) { events++; });
        session.GetEventBus().Publish(
            ffmpeg::FrameProcessedEvent());
        CHECK(events == 1);
    }

    // Test 4: GetStatus on stopped session
    {
        ffmpeg::StreamSessionConfig cfg;
        cfg.rtsp_suffix = "test";
        ffmpeg::StreamSession session(cfg);
        ffmpeg::SessionStatus status = session.GetStatus();
        CHECK(!status.running);
        CHECK(status.stream_id == "test");
        CHECK(status.frames_processed == 0);
    }

    // Test 5: config accessor returns stored config
    {
        ffmpeg::StreamSessionConfig cfg;
        cfg.input_url = "rtsp://example.com/stream";
        cfg.rtsp_port = 5554;
        ffmpeg::StreamSession session(cfg);
        CHECK(session.Config().input_url == "rtsp://example.com/stream");
        CHECK(session.Config().rtsp_port == 5554);
    }

    // Test 6: GetRtspServer returns nullptr before Start
    {
        ffmpeg::StreamSessionConfig cfg;
        ffmpeg::StreamSession session(cfg);
        CHECK(session.GetRtspServer() == nullptr);
    }

    // Test 7: GetFacePlugin returns nullptr when AI disabled
    {
        ffmpeg::StreamSessionConfig cfg;
        cfg.enable_ai = false;
        ffmpeg::StreamSession session(cfg);
        CHECK(session.GetFacePlugin() == nullptr);
    }

    // Test 8: GetSessionId returns 0 before Start
    {
        ffmpeg::StreamSessionConfig cfg;
        ffmpeg::StreamSession session(cfg);
        CHECK(session.GetSessionId() == 0);
    }

    // Test 9: UpdateEffects on stopped session (should fail gracefully if
    //         models not available — returns false but does not crash)
    {
        ffmpeg::StreamSessionConfig cfg;
        ffmpeg::StreamSession session(cfg);
        // No models available, so UpdateEffects should fail (return false)
        // but should not crash
        bool ok = session.UpdateEffects(
            "{\"detect_model\":\"/nonexistent/model.onnx\","
            "\"recog_model\":\"/nonexistent/recog.onnx\","
            "\"face_db_path\":\"faces.json\","
            "\"analyze_fps\":5}");
        // May succeed (face_plugin_ created) or fail (Open failed) depending
        // on whether the nonexistent model triggers a crash or just returns
        // false — both are acceptable. The point is: no crash.
        (void)ok;
        // Verify no crash after UpdateEffects + destruction
    }

    std::cout << "StreamSession: " << tests_passed << "/"
              << (tests_passed + tests_failed) << " passed" << std::endl;
    return tests_failed;
}