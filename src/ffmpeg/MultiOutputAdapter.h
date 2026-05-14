// MultiOutputAdapter.h
// Composite adapter that writes to multiple output adapters simultaneously.

#ifndef FFMPEG_MULTI_OUTPUT_ADAPTER_H
#define FFMPEG_MULTI_OUTPUT_ADAPTER_H

#include "IOutputAdapter.h"
#include <vector>
#include <memory>
#include <algorithm>

namespace ffmpeg {

class MultiOutputAdapter : public IOutputAdapter {
public:
    void AddAdapter(std::shared_ptr<IOutputAdapter> adapter) {
        adapters_.push_back(std::move(adapter));
    }

    bool Open(const AVCodecContext* enc_ctx,
              const OutputConfig& cfg) override {
        for (auto& a : adapters_) {
            if (!a->Open(enc_ctx, cfg)) return false;
        }
        return true;
    }

    void Close() override {
        for (auto& a : adapters_) a->Close();
    }

    bool WritePacket(const AVPacket* pkt) override {
        for (auto& a : adapters_) {
            if (!a->WritePacket(pkt)) return false;
        }
        return true;
    }

    bool IsOpened() const override {
        return std::all_of(adapters_.begin(), adapters_.end(),
                           [](const std::shared_ptr<IOutputAdapter>& a) {
                               return a->IsOpened();
                           });
    }

    size_t Count() const { return adapters_.size(); }

private:
    std::vector<std::shared_ptr<IOutputAdapter>> adapters_;
};

} // namespace ffmpeg

#endif // FFMPEG_MULTI_OUTPUT_ADAPTER_H
