// EffectChain.h
// Ordered chain of IEffectPlugin instances executed sequentially.
// Each frame passes through every plugin in insertion order.

#ifndef EFFECT_EFFECT_CHAIN_H
#define EFFECT_EFFECT_CHAIN_H

#include "IEffectPlugin.h"
#include <vector>
#include <memory>

namespace streamsight {

class EffectChain {
public:
    void AddPlugin(std::shared_ptr<IEffectPlugin> plugin) {
        plugins_.push_back(std::move(plugin));
    }

    // Process one BGR24 frame through all plugins in order.
    // frame data is modified in-place by overlay/transform plugins.
    // results is appended to by all plugins that produce output.
    // Returns false if any plugin returns false.
    bool ProcessFrame(uint8_t* bgr_data, int width, int height,
                      int linesize, std::vector<EffectResult>& results) {
        for (auto& p : plugins_) {
            EffectResult r;
            if (!p->Process(bgr_data, width, height, linesize, &r))
                return false;
            if (!r.plugin_name.empty())
                results.push_back(std::move(r));
        }
        return true;
    }

    size_t Size() const { return plugins_.size(); }
    bool   Empty() const { return plugins_.empty(); }

    void Clear() { plugins_.clear(); }

private:
    std::vector<std::shared_ptr<IEffectPlugin>> plugins_;
};

}  // namespace streamsight

#endif  // EFFECT_EFFECT_CHAIN_H