#include "control/StreamManager.h"
#include <iostream>

namespace control {

StreamManager::StreamManager(std::shared_ptr<Scheduler> scheduler,
                             std::shared_ptr<cdn_sim::EdgeNodePool> node_pool,
                             observe::MetricsRegistry* metrics,
                             const std::string& bind_ip,
                             int rtsp_port)
    : scheduler_(std::move(scheduler)),
      node_pool_(std::move(node_pool)),
      metrics_(metrics),
      bind_ip_(bind_ip),
      rtsp_port_(rtsp_port) {
}

bool StreamManager::Start() {
    event_loop_.reset(new xop::EventLoop());
    rtsp_server_ = xop::RtspServer::Create(event_loop_.get());
    if (!rtsp_server_ || !rtsp_server_->Start(bind_ip_, rtsp_port_)) {
        std::cerr << "[StreamManager] RTSP bind failed on " << bind_ip_ << ":" << rtsp_port_ << std::endl;
        return false;
    }
    std::cout << "[StreamManager] RTSP server listening on " << bind_ip_ << ":" << rtsp_port_ << std::endl;
    return true;
}

bool StreamManager::StartStream(StreamTask task) {
    classifier_.Apply(task);

    auto node = scheduler_->SelectNode(task);
    if (!node) {
        std::cerr << "[StreamManager] No available node for stream=" << task.stream_id << std::endl;
        return false;
    }

    auto ctx = std::make_shared<StreamContext>();
    ctx->task = task;
    ctx->stop_flag = std::make_shared<std::atomic<bool>>(false);
    ctx->status = StreamStatus::Pending;

    {
        std::lock_guard<std::mutex> lk(mu_);
        streams_[task.stream_id] = ctx;
    }

    Dispatch(ctx, node);
    return true;
}

void StreamManager::Dispatch(const std::shared_ptr<StreamContext>& ctx,
                             const std::shared_ptr<cdn_sim::EdgeNode>& node) {
    ctx->node_id = node->Id();
    ctx->status = StreamStatus::Running;

    if (metrics_) {
        metrics_->SetGauge("stream." + ctx->task.stream_id, "failover_count", ctx->failover_count);
        metrics_->SetGauge("stream." + ctx->task.stream_id, "assigned_node_hash", 1.0);
        metrics_->IncCounter("scheduler", "dispatch_total", 1.0);
    }

    std::cout << "[Dispatch] stream=" << ctx->task.stream_id
              << " -> node=" << ctx->node_id
              << " region=" << ToString(ctx->task.region)
              << " bitrate=" << ToString(ctx->task.bitrate_class)
              << " compute=" << ToString(ctx->task.compute_class)
              << std::endl;

    const bool ok = node->Submit(ctx->task, [this, ctx]() {
        PipelineRunner runner(ctx->task, rtsp_server_.get(), metrics_, ctx->node_id);
        PipelineResult result = runner.Run(*ctx->stop_flag);
        OnStreamExit(ctx, result);
    });

    if (!ok) {
        ctx->status = StreamStatus::Failed;
        std::cerr << "[Dispatch] submit failed for stream=" << ctx->task.stream_id
                  << " node=" << node->Id() << std::endl;
    }
}

void StreamManager::OnStreamExit(const std::shared_ptr<StreamContext>& ctx,
                                 const PipelineResult& result) {
    if (ctx->stop_flag->load()) {
        ctx->status = StreamStatus::Stopped;
        std::cout << "[StreamExit] stream=" << ctx->task.stream_id << " stopped." << std::endl;
        return;
    }

    if (result.code == PipelineExitCode::Failed &&
        ctx->failover_count < ctx->task.max_failover) {
        auto next_node = scheduler_->SelectNode(ctx->task, ctx->node_id);
        if (next_node) {
            ++ctx->failover_count;
            if (metrics_) {
                metrics_->IncCounter("scheduler", "failover_total", 1.0);
                metrics_->SetGauge("stream." + ctx->task.stream_id, "failover_count", ctx->failover_count);
            }
            std::cout << "[Failover] stream=" << ctx->task.stream_id
                      << " from=" << ctx->node_id
                      << " to=" << next_node->Id()
                      << " reason=" << result.message << std::endl;
            Dispatch(ctx, next_node);
            return;
        }
    }

    ctx->status = (result.code == PipelineExitCode::Completed)
        ? StreamStatus::Completed
        : StreamStatus::Failed;

    std::cout << "[StreamExit] stream=" << ctx->task.stream_id
              << " status=" << (ctx->status == StreamStatus::Completed ? "completed" : "failed")
              << " msg=" << result.message << std::endl;
}

void StreamManager::StopStream(const std::string& stream_id) {
    std::shared_ptr<StreamContext> ctx;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = streams_.find(stream_id);
        if (it != streams_.end()) {
            ctx = it->second;
        }
    }
    if (ctx) {
        ctx->stop_flag->store(true);
    }
}

void StreamManager::StopAll() {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& kv : streams_) {
        kv.second->stop_flag->store(true);
    }
}

} // namespace control
