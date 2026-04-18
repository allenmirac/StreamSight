#pragma once
#include "cdn_sim/ThreadPool.h"
#include "control/PolicyCenter.h"
#include "control/StreamTask.h"
#include "observe/MetricsRegistry.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace cdn_sim {

enum class EdgeNodeType {
    HighCapacity,
    MediumCapacity,
    LowCapacity
};

enum class NodeStatus {
    Healthy,
    Busy,
    Degraded,
    Down
};

struct EdgeNodeSpec {
    std::string node_id;
    control::Region region { control::Region::Local };
    EdgeNodeType type { EdgeNodeType::MediumCapacity };
    int worker_threads { 4 };
    int max_streams    { 4 };
    int max_queue      { 16 };
    EdgeNodeSpec(std::string node_id_, control::Region region_, EdgeNodeType type_,
         int worker_threads_, int max_streams, int max_queue_)
        : node_id(node_id_), region(region_), type(type_), 
        worker_threads(worker_threads_), max_streams(max_streams), max_queue(max_queue_) {

    }
};

struct EdgeNodeStats {
    int total_workers { 0 };
    int active_workers { 0 };
    int pending_tasks { 0 };
    int active_streams { 0 };
    double worker_util { 0.0 };
    double cpu_usage_estimate { 0.0 };
    double avg_pipeline_latency_ms { 0.0 };
};

class EdgeNode {
public:
    EdgeNode(const EdgeNodeSpec& spec, observe::MetricsRegistry* metrics);

    const std::string& Id() const { return spec_.node_id; }
    control::Region RegionTag() const { return spec_.region; }
    EdgeNodeType Type() const { return spec_.type; }

    bool CanAccept(const control::StreamTask& task,
                   const control::SchedulerPolicy& policy) const;

    double Score(const control::StreamTask& task,
                 const control::SchedulerPolicy& policy) const;

    bool Submit(const control::StreamTask& task, std::function<void()> fn);

    EdgeNodeStats Stats() const;

    void MarkHealthy() { status_ = NodeStatus::Healthy; }
    void MarkDegraded() { status_ = NodeStatus::Degraded; }
    void MarkDown() { status_ = NodeStatus::Down; }
    NodeStatus Status() const { return status_.load(); }

private:
    int RegionDistance(control::Region a, control::Region b) const;
    double CapabilityPenalty(const control::StreamTask& task) const;

private:
    EdgeNodeSpec spec_;
    observe::MetricsRegistry* metrics_ { nullptr };
    ThreadPool workers_;
    std::atomic<int> active_streams_ { 0 };
    std::atomic<NodeStatus> status_ { NodeStatus::Healthy };
};

} // namespace cdn_sim
