#pragma once
#include "cdn_sim/EdgeNodePool.h"
#include "control/Classifier.h"
#include "control/PipelineRunner.h"
#include "control/Scheduler.h"
#include "observe/MetricsRegistry.h"
#include "xop/RtspServer.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace control {

class StreamManager {
public:
    StreamManager(std::shared_ptr<Scheduler> scheduler,
                  std::shared_ptr<cdn_sim::EdgeNodePool> node_pool,
                  observe::MetricsRegistry* metrics,
                  const std::string& bind_ip,
                  int rtsp_port);

    bool Start();
    bool StartStream(StreamTask task);
    void StopStream(const std::string& stream_id);
    void StopAll();

private:
    struct StreamContext {
        StreamTask task;
        std::shared_ptr<std::atomic<bool>> stop_flag;
        std::string node_id;
        StreamStatus status { StreamStatus::Pending };
        int failover_count { 0 };
    };

    void Dispatch(const std::shared_ptr<StreamContext>& ctx,
                  const std::shared_ptr<cdn_sim::EdgeNode>& node);

    void OnStreamExit(const std::shared_ptr<StreamContext>& ctx,
                      const PipelineResult& result);

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<StreamContext>> streams_;

    Classifier classifier_;
    std::shared_ptr<Scheduler> scheduler_;
    std::shared_ptr<cdn_sim::EdgeNodePool> node_pool_;
    observe::MetricsRegistry* metrics_ { nullptr };

    std::shared_ptr<xop::EventLoop> event_loop_;
    std::shared_ptr<xop::RtspServer> rtsp_server_;

    std::string bind_ip_;
    int rtsp_port_ { 554 };
};

} // namespace control
