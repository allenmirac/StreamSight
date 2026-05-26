// tests/test_effect_factory.cpp
#include "../src/effect/EffectFactory.h"
#include "../src/effect/IEffectPlugin.h"
#include "../src/effect/EffectChain.h"
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
int run_effect_factory_tests() {
#endif
    // Test 1: create known plugin via JSON config
    {
        std::string json = R"({
            "detect_model": "models/face_detection.onnx",
            "recog_model": "models/face_recognition.onnx",
            "face_db_path": "faces.json",
            "event_log_path": "events.jsonl",
            "analyze_fps": 5
        })";
        auto plugin = streamsight::EffectFactory::Create("FaceRecognition", json);
        CHECK(plugin != nullptr);
        CHECK(plugin->Name() == "FaceRecognition");
        CHECK(plugin->Category() == streamsight::EffectCategory::Analysis);
        CHECK(plugin->ModifiesFrame() == true);
        bool opened = plugin->Open(json);
        // If models don't exist, Open may return false — that's OK
        plugin->Close();
    }

    // Test 2: unknown plugin returns nullptr
    {
        auto plugin = streamsight::EffectFactory::Create("NonExistent", "{}");
        CHECK(plugin == nullptr);
    }

    // Test 3: round-trip through EffectChain
    {
        streamsight::EffectChain chain;
        std::string json = R"({
            "detect_model": "models/nonexistent.onnx",
            "recog_model": "models/nonexistent.onnx",
            "face_db_path": "/tmp/test_faces.json",
            "event_log_path": "",
            "analyze_fps": 5
        })";
        auto plugin = streamsight::EffectFactory::Create("FaceRecognition", json);
        if (plugin) {
            chain.AddPlugin(plugin);
            CHECK(chain.Size() == 1);
            chain.Clear();
            CHECK(chain.Empty());
        }
    }

    std::cout << "EffectFactory: " << tests_passed << "/" << (tests_passed + tests_failed) << " passed" << std::endl;
    return tests_failed;
}