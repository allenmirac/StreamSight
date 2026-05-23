// FaceRecognitionPlugin.cpp
// Implementation wrapping existing ai/ classes.

#include "FaceRecognitionPlugin.h"
#include "ai/FaceDetector.h"
#include "ai/FaceRecognizer.h"
#include "ai/FaceDatabase.h"
#include "ai/FrameAnalyzer.h"
#include "ai/FrameOverlay.h"
#include "ai/EventLogger.h"

#include <opencv2/opencv.hpp>
#include <iostream>

namespace streamsight {

FaceRecognitionPlugin::FaceRecognitionPlugin(const Config& cfg)
    : cfg_(cfg) {}

FaceRecognitionPlugin::~FaceRecognitionPlugin() {
    Close();
}

bool FaceRecognitionPlugin::Open(const std::string& /*config_json*/) {
    if (opened_) return true;

    detector_.reset(new ai::FaceDetector(cfg_.detect_model));
    if (!detector_->Load()) {
        std::cerr << "[FaceRecognitionPlugin] detector load failed: "
                  << cfg_.detect_model << std::endl;
        detector_.reset();
    }

    recognizer_.reset(new ai::FaceRecognizer(cfg_.recog_model));
    if (!recognizer_->Load()) {
        std::cerr << "[FaceRecognitionPlugin] recognizer load failed: "
                  << cfg_.recog_model << std::endl;
        recognizer_.reset();
    }

    database_.reset(new ai::FaceDatabase(cfg_.face_db_path));
    database_->Load();

    analyzer_.reset(new ai::FrameAnalyzer(
        detector_.get(), recognizer_.get(), database_.get()));
    analyzer_->SetAnalyzeRate(cfg_.analyze_fps);

    overlay_.reset(new ai::FrameOverlay());

    if (!cfg_.event_log_path.empty()) {
        logger_.reset(new ai::EventLogger(cfg_.event_log_path));
        logger_->Open();
        analyzer_->SetEventCallback([this](const ai::AnalysisResult& r) {
            if (logger_) logger_->Log(r);
        });
    }

    opened_ = true;
    std::cout << "[FaceRecognitionPlugin] opened" << std::endl;
    return true;
}

void FaceRecognitionPlugin::Close() {
    if (!opened_) return;
    if (logger_)   logger_->Close();
    if (database_) database_->Save();
    opened_ = false;
}

bool FaceRecognitionPlugin::Process(uint8_t* bgr_data, int width, int height,
                                    int linesize, EffectResult* result) {
    if (!opened_ || !analyzer_ || !overlay_) return false;

    // Wrap raw BGR data as cv::Mat (no copy — use existing buffer)
    cv::Mat mat(height, width, CV_8UC3, bgr_data, (size_t)linesize);

    ai::AnalysisResult analysis = analyzer_->Analyze(mat);
    overlay_->Draw(mat, analysis);

    if (result) {
        result->plugin_name = Name();
        // Build minimal JSON: {"faces":[{"name":"Alice","conf":0.95},...]}
        std::string json = "{\"faces\":[";
        for (size_t i = 0; i < analysis.faces.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"name\":\"" + analysis.faces[i].name + "\""
                  + ",\"conf\":" + std::to_string(analysis.faces[i].confidence)
                  + ",\"sim\":" + std::to_string(analysis.faces[i].similarity)
                  + "}";
        }
        json += "]}";
        result->json_data = json;
    }

    return true;
}

const ai::AnalysisResult& FaceRecognitionPlugin::GetLastResult() const {
    static ai::AnalysisResult empty;
    if (!analyzer_) return empty;
    return analyzer_->GetLastResult();
}

ai::FaceDatabase* FaceRecognitionPlugin::GetDatabase() const {
    return database_.get();
}

ai::FaceRecognizer* FaceRecognitionPlugin::GetRecognizer() const {
    return recognizer_.get();
}

}  // namespace streamsight