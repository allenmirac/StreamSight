#pragma once
#include <string>

namespace control {

enum class SourceType {
    Camera,
    File,
    Rtsp
};

enum class Region {
    Local,
    North,
    East,
    South,
    West
};

enum class BitrateClass {
    Low,
    Medium,
    High,
    Ultra
};

enum class LatencyClass {
    Realtime,
    Interactive,
    Standard,
    Archive
};

enum class ComputeClass {
    Light,
    Medium,
    Heavy,
    Extreme
};

enum class StreamStatus {
    Pending,
    Running,
    Failed,
    Completed,
    Stopped
};

inline const char* ToString(Region v) {
    switch (v) {
    case Region::Local: return "local";
    case Region::North: return "north";
    case Region::East:  return "east";
    case Region::South: return "south";
    case Region::West:  return "west";
    }
    return "local";
}

inline const char* ToString(BitrateClass v) {
    switch (v) {
    case BitrateClass::Low:    return "low";
    case BitrateClass::Medium: return "medium";
    case BitrateClass::High:   return "high";
    case BitrateClass::Ultra:  return "ultra";
    }
    return "medium";
}

inline const char* ToString(LatencyClass v) {
    switch (v) {
    case LatencyClass::Realtime:    return "realtime";
    case LatencyClass::Interactive: return "interactive";
    case LatencyClass::Standard:    return "standard";
    case LatencyClass::Archive:     return "archive";
    }
    return "standard";
}

inline const char* ToString(ComputeClass v) {
    switch (v) {
    case ComputeClass::Light:   return "light";
    case ComputeClass::Medium:  return "medium";
    case ComputeClass::Heavy:   return "heavy";
    case ComputeClass::Extreme: return "extreme";
    }
    return "medium";
}

inline Region ParseRegion(const std::string& s) {
    if (s == "north") return Region::North;
    if (s == "east")  return Region::East;
    if (s == "south") return Region::South;
    if (s == "west")  return Region::West;
    return Region::Local;
}

inline SourceType ParseSourceType(const std::string& s) {
    if (s == "camera") return SourceType::Camera;
    if (s == "rtsp")   return SourceType::Rtsp;
    return SourceType::File;
}

struct StreamTask {
    std::string stream_id;
    std::string session_suffix;
    std::string source_uri;

    SourceType source_type { SourceType::File };
    int camera_device { 0 };

    int width  { 640 };
    int height { 480 };
    int fps    { 25 };

    int rtsp_port { 554 };
    int http_port { 8080 };
    int target_bitrate_kbps { 2048 };

    bool loop_input      { true };
    bool enable_ai       { true };
    bool enable_overlay  { true };
    bool enable_record   { false };
    bool enable_rtsp_push{ true };

    std::string detect_model { "models/face_detection.onnx" };
    std::string recog_model  { "models/face_recognition.onnx" };
    std::string face_db_path { "faces.json" };
    std::string log_path     { "events.jsonl" };

    int analyze_fps { 5 };

    Region       region        { Region::Local };
    BitrateClass bitrate_class { BitrateClass::Medium };
    LatencyClass latency_class { LatencyClass::Realtime };
    ComputeClass compute_class { ComputeClass::Medium };

    int priority     { 100 };
    int max_failover { 2 };

    bool use_ffmpeg_pipeline { false };  // use StreamPipeline (parallel) instead of legacy
};

} // namespace control
