#include "control/Classifier.h"

namespace control {

void Classifier::Apply(StreamTask& task) const {
    if (task.target_bitrate_kbps <= 1000) {
        task.bitrate_class = BitrateClass::Low;
    } else if (task.target_bitrate_kbps <= 4000) {
        task.bitrate_class = BitrateClass::Medium;
    } else if (task.target_bitrate_kbps <= 8000) {
        task.bitrate_class = BitrateClass::High;
    } else {
        task.bitrate_class = BitrateClass::Ultra;
    }

    const int pixels = task.width * task.height;

    if (!task.enable_ai) {
        if (pixels <= 640 * 480 && task.fps <= 25) {
            task.compute_class = ComputeClass::Light;
        } else {
            task.compute_class = ComputeClass::Medium;
        }
    } else {
        if (pixels >= 1920 * 1080 || task.fps >= 30 || task.analyze_fps >= 15) {
            task.compute_class = ComputeClass::Extreme;
        } else if (pixels >= 1280 * 720 || task.analyze_fps >= 10) {
            task.compute_class = ComputeClass::Heavy;
        } else {
            task.compute_class = ComputeClass::Medium;
        }
    }

    if (task.enable_record && !task.enable_ai) {
        task.latency_class = LatencyClass::Archive;
    } else if (task.enable_ai) {
        task.latency_class = LatencyClass::Realtime;
    } else {
        task.latency_class = LatencyClass::Standard;
    }

    task.priority = 100;
    if (task.latency_class == LatencyClass::Realtime) task.priority -= 30;
    if (task.compute_class == ComputeClass::Heavy)    task.priority -= 10;
    if (task.compute_class == ComputeClass::Extreme)  task.priority -= 20;

    if (task.session_suffix.empty()) {
        task.session_suffix = task.stream_id;
    }
}

} // namespace control
