#include <gtest/gtest.h>

#include <string>

#include "effect/EffectChain.h"
#include "effect/EffectFactory.h"
#include "effect/IEffectPlugin.h"

TEST(EffectFactory, CreatesFaceRecognitionPlugin) {
  const std::string json = R"({
    "detect_model": "/nonexistent/detect.onnx",
    "recog_model": "/nonexistent/recog.onnx",
    "face_db_path": "/tmp/streamsight_effect_test_faces.json",
    "event_log_path": "",
    "analyze_fps": 5
  })";

  auto plugin =
      streamsight::effect::EffectFactory::Create("FaceRecognition", json);
  ASSERT_NE(plugin, nullptr);
  EXPECT_EQ(plugin->Name(), "FaceRecognition");
  EXPECT_EQ(plugin->Category(), streamsight::effect::EffectCategory::Analysis);
  EXPECT_TRUE(plugin->ModifiesFrame());

  // Models do not exist, so Open must fail deterministically.
  EXPECT_FALSE(plugin->Open(""));
  plugin->Close();
}

TEST(EffectFactory, UnknownPluginReturnsNull) {
  auto plugin = streamsight::effect::EffectFactory::Create("NonExistent", "{}");
  EXPECT_EQ(plugin, nullptr);
}

TEST(EffectChain, AddAndClear) {
  streamsight::effect::EffectChain chain;
  auto plugin = streamsight::effect::EffectFactory::Create(
      "FaceRecognition",
      R"({"detect_model":"/nonexistent/d.onnx",)"
      R"("recog_model":"/nonexistent/r.onnx",)"
      R"("face_db_path":"/tmp/streamsight_chain_faces.json"})");
  ASSERT_NE(plugin, nullptr);

  chain.AddPlugin(plugin);
  EXPECT_EQ(chain.Size(), 1u);
  EXPECT_FALSE(chain.Empty());

  chain.Clear();
  EXPECT_EQ(chain.Size(), 0u);
  EXPECT_TRUE(chain.Empty());
}
