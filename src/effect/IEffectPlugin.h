// IEffectPlugin.h
// Abstract interface for video effect / analysis plugins.
// Plugins receive raw BGR24 frame data and may modify it in-place
// and/or produce structured analysis results.
//
// Part of the EffectPlugin system introduced in Phase 1.

#ifndef EFFECT_I_EFFECT_PLUGIN_H
#define EFFECT_I_EFFECT_PLUGIN_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace streamsight {

enum class EffectCategory {
    Analysis,   // Face detection, object detection, content moderation
    Overlay,    // Watermark, sticker, bounding-box rendering
    Transform,  // Beauty filter, color correction, mosaic
    Extract,    // Keyframe extraction, summary frame capture
};

struct EffectResult {
    std::string plugin_name;
    std::string json_data;
};

class IEffectPlugin {
public:
    virtual ~IEffectPlugin() = default;

    virtual std::string    Name()     const = 0;
    virtual EffectCategory Category() const = 0;

    // Called once before any Process() call. config_json is plugin-specific.
    virtual bool Open(const std::string& config_json) = 0;

    // Called once when the plugin is no longer needed.
    virtual void Close() = 0;

    // Process one BGR24 frame. Frame data is modified in-place for overlay
    // plugins. Structured results are appended to |result| (nullable).
    // Returns false on error (plugin will be skipped for remaining frames).
    virtual bool Process(uint8_t* bgr_data, int width, int height,
                         int linesize, EffectResult* result) = 0;

    // If true, the frame data passed to Process() will be modified.
    // If false, Process() only produces metadata (no pixel change).
    virtual bool ModifiesFrame() const = 0;
};

}  // namespace streamsight

#endif  // EFFECT_I_EFFECT_PLUGIN_H