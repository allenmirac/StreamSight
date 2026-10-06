// FaceRecognitionPlugin.h
// IEffectPlugin that wraps streamsight::ai::FaceDetector +
// streamsight::ai::FaceRecognizer + streamsight::ai::FaceDatabase +
// streamsight::ai::FrameAnalyzer + streamsight::ai::FrameOverlay.
//
// Category: Analysis + Overlay (both detects faces and draws bounding boxes).

#ifndef STREAMSIGHT_EFFECT_FACE_RECOGNITION_PLUGIN_H
#define STREAMSIGHT_EFFECT_FACE_RECOGNITION_PLUGIN_H

#include <memory>
#include <string>

#include "IEffectPlugin.h"

namespace cv {
class Mat;
}

namespace streamsight::ai {
class FaceDetector;
class FaceRecognizer;
class FaceDatabase;
class FrameAnalyzer;
class FrameOverlay;
class EventLogger;
struct AnalysisResult;
}  // namespace streamsight::ai

namespace streamsight::effect {

class FaceRecognitionPlugin : public IEffectPlugin {
 public:
  struct Config {
    std::string detect_model;
    std::string recog_model;
    std::string face_db_path;
    std::string event_log_path;
    int analyze_fps = 5;
  };

  explicit FaceRecognitionPlugin(const Config& cfg);
  ~FaceRecognitionPlugin() override;

  std::string Name() const override { return "FaceRecognition"; }
  EffectCategory Category() const override { return EffectCategory::Analysis; }

  bool Open(const std::string& config_json) override;
  void Close() override;

  bool Process(uint8_t* bgr_data, int width, int height, int linesize,
               EffectResult* result) override;

  bool ModifiesFrame() const override { return true; }

  // Access underlying results (for API serving)
  streamsight::ai::AnalysisResult GetLastResult() const;

  // Expose internal components as raw pointers for StreamApiServer.
  // StreamApiServer does not own these — FaceRecognitionPlugin keeps them
  // alive.
  streamsight::ai::FaceDatabase* GetDatabase() const;
  streamsight::ai::FaceRecognizer* GetRecognizer() const;

 private:
  Config cfg_;
  bool opened_ = false;

  std::unique_ptr<streamsight::ai::FaceDetector> detector_;
  std::unique_ptr<streamsight::ai::FaceRecognizer> recognizer_;
  std::unique_ptr<streamsight::ai::FaceDatabase> database_;
  std::unique_ptr<streamsight::ai::FrameAnalyzer> analyzer_;
  std::unique_ptr<streamsight::ai::FrameOverlay> overlay_;
  std::unique_ptr<streamsight::ai::EventLogger> logger_;
};

}  // namespace streamsight::effect

#endif  // STREAMSIGHT_EFFECT_FACE_RECOGNITION_PLUGIN_H