#include "cdn_sim/EdgeNode.h"
#include "observe/LatencyTracer.h"
#include <algorithm>

namespace cdn_sim {

EdgeNode::EdgeNode(const EdgeNodeSpec& spec, observe::MetricsRegistry* metrics)
    : spec_(spec), metrics_(metrics), workers_(spec.worker_threads) {
}

int EdgeNode::RegionDistance(control::Region a, control::Region b) const {
    if (a == b) return 0;
    if (a == control::Region::Local || b == control::Region::Local) return 1;
    return 2;
}

double EdgeNode::CapabilityPenalty(const control::StreamTask& task) const {
    double penalty = 0.0;

    if (task.compute_class == control::ComputeClass::Extreme) {
        if (spec_.type == EdgeNodeType::LowCapacity) penalty += 1.0;
        else if (spec_.type == EdgeNodeType::MediumCapacity) penalty += 0.5;
    } else if (task.compute_class == control::ComputeClass::Heavy) {
        if (spec_.type == EdgeNodeType::LowCapacity) penalty += 0.8;
        else if (spec_.type == EdgeNodeType::MediumCapacity) penalty += 0.3;
    } else if (task.compute_class == control::ComputeClass::Medium) {
        if (spec_.type == EdgeNodeType::LowCapacity) penalty += 0.2;
    }

    if (task.bitrate_class == control::BitrateClass::Ultra) {
        if (spec_.type != EdgeNodeType::HighCapacity) penalty += 0.4;
    } else if (task.bitrate_class == control::BitrateClass::High) {
        if (spec_.type == EdgeNodeType::LowCapacity) penalty += 0.3;
    }

    return std::min(1.0, penalty);
}

EdgeNodeStats EdgeNode::Stats() const {
    EdgeNodeStats s;
    s.total_workers = static_cast<int>(workers_.Size());
    s.active_workers = static_cast<int>(workers_.ActiveWorkers());
    s.pending_tasks = static_cast<int>(workers_.PendingTasks());
    s.active_streams = active_streams_.load();

    s.worker_util = (s.total_workers == 0)
        ? 0.0
        : static_cast<double>(s.active_workers) / s.total_workers;

    double queue_util = (spec_.max_queue == 0)
        ? 0.0
        : static_cast<double>(s.pending_tasks) / spec_.max_queue;

    s.cpu_usage_estimate = std::min(100.0, 100.0 * (0.7 * s.worker_util + 0.3 * queue_util));

    if (metrics_) {
        s.avg_pipeline_latency_ms =
            metrics_->GetMetric("node." + spec_.node_id, "avg_pipeline_latency_ms");
    }

    return s;
}

bool EdgeNode::CanAccept(const control::StreamTask& task,
                         const control::SchedulerPolicy& policy) const {
    (void)task;

    if (status_ == NodeStatus::Down) return false;

    const auto st = Stats();
    if (st.active_streams >= spec_.max_streams) return false;
    if (st.pending_tasks >= policy.max_queue_threshold) return false;
    if (st.worker_util >= policy.busy_util_threshold) return false;

    if (CapabilityPenalty(task) >= 0.95) return false;
    return true;
}

double EdgeNode::Score(const control::StreamTask& task,
                       const control::SchedulerPolicy& policy) const {
    const auto st = Stats();

    const double region_score =
        static_cast<double>(RegionDistance(task.region, spec_.region)) / 2.0;

    const double load_score =
        std::min(1.0, 0.6 * st.worker_util +
                        0.4 * (static_cast<double>(st.pending_tasks) / std::max(1, spec_.max_queue)));

    const double capability_score = CapabilityPenalty(task);

    double latency_score = std::min(1.0, st.avg_pipeline_latency_ms / 100.0);
    if (task.latency_class == control::LatencyClass::Realtime) {
        latency_score *= 1.2;
    }

    double failover_penalty = 0.0;
    if (status_ == NodeStatus::Busy) failover_penalty = 0.4;
    if (status_ == NodeStatus::Degraded) failover_penalty = 0.7;
    if (status_ == NodeStatus::Down) failover_penalty = 1.0;

    return policy.region_weight     * region_score +
           policy.load_weight       * load_score +
           policy.capability_weight * capability_score +
           policy.latency_weight    * latency_score +
           policy.failover_weight   * failover_penalty;
}

bool EdgeNode::Submit(const control::StreamTask& task, std::function<void()> fn) {
    STREAMSIGHT_LATENCY_SCOPE("cdn_sim", "edge_node_enqueue");
    if (status_ == NodeStatus::Down) return false;

    ++active_streams_;
    if (metrics_) {
        metrics_->SetGauge("node." + spec_.node_id, "active_streams", active_streams_.load());
        metrics_->IncCounter("node." + spec_.node_id, "dispatch_count", 1.0);
        metrics_->IncCounter("stream." + task.stream_id, "dispatch_count", 1.0);
    }

    const bool ok = workers_.Submit([this, fn, task]() {
        try {
            fn();
        } catch (...) {
        }

        --active_streams_;
        if (metrics_) {
            metrics_->SetGauge("node." + spec_.node_id, "active_streams", active_streams_.load());
        }
    });

    if (!ok) {
        --active_streams_;
    }
    return ok;
}

} // namespace cdn_sim
