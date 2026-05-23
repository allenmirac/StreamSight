// FaceRecognitionPlugin.h
// IEffectPlugin that wraps ai::FaceDetector + ai::FaceRecognizer +
// ai::FaceDatabase + ai::FrameAnalyzer + ai::FrameOverlay.
//
// Category: Analysis + Overlay (both detects faces and draws bounding boxes).

#ifndef EFFECT_FACE_RECOGNITION_PLUGIN_H
#define EFFECT_FACE_RECOGNITION_PLUGIN_H

#include "IEffectPlugin.h"
#include <memory>
#include <string>

namespace cv {
class Mat;
}

namespace ai {
class FaceDetector;
class FaceRecognizer;
class FaceDatabase;
class FrameAnalyzer;
class FrameOverlay;
class EventLogger;
struct AnalysisResult;
}

namespace streamsight {

class FaceRecognitionPlugin : public IEffectPlugin {
public:
    struct Config {
        std::string detect_model;
        std::string recog_model;
        std::string face_db_path;
        std::string event_log_path;
        int         analyze_fps = 5;
    };

    explicit FaceRecognitionPlugin(const Config& cfg);
    ~FaceRecognitionPlugin() override;

    std::string    Name()     const override { return "FaceRecognition"; }
    EffectCategory Category() const override { return EffectCategory::Analysis; }

    bool Open(const std::string& config_json) override;
    void Close() override;

    bool Process(uint8_t* bgr_data, int width, int height,
                 int linesize, EffectResult* result) override;

    bool ModifiesFrame() const override { return true; }

    // Access underlying results (for API serving)
    const ai::AnalysisResult& GetLastResult() const;

    // Expose internal components as raw pointers for HttpApiServer.
    // HttpApiServer does not own these — FaceRecognitionPlugin keeps them alive.
    ai::FaceDatabase*   GetDatabase()   const;
    ai::FaceRecognizer* GetRecognizer() const;

private:
    Config cfg_;
    bool   opened_ = false;

    std::unique_ptr<ai::FaceDetector>   detector_;
    std::unique_ptr<ai::FaceRecognizer> recognizer_;
    std::unique_ptr<ai::FaceDatabase>   database_;
    std::unique_ptr<ai::FrameAnalyzer>  analyzer_;
    std::unique_ptr<ai::FrameOverlay>   overlay_;
    std::unique_ptr<ai::EventLogger>    logger_;
};

}  // namespace streamsight

#endif  // EFFECT_FACE_RECOGNITION_PLUGIN_H