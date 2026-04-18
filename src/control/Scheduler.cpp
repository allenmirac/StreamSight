#include "control/Scheduler.h"
#include <limits>

namespace control {

Scheduler::Scheduler(std::shared_ptr<cdn_sim::EdgeNodePool> pool,
                     const SchedulerPolicy& policy)
    : pool_(std::move(pool)), policy_(policy) {
}

std::shared_ptr<cdn_sim::EdgeNode> Scheduler::SelectNode(
    const StreamTask& task,
    const std::string& exclude_node) const {
    auto nodes = pool_->AllNodes();

    std::shared_ptr<cdn_sim::EdgeNode> best;
    double best_score = std::numeric_limits<double>::max();

    for (auto& node : nodes) {
        if (!exclude_node.empty() && node->Id() == exclude_node) {
            continue;
        }
        if (!node->CanAccept(task, policy_)) {
            continue;
        }

        const double score = node->Score(task, policy_);
        if (score < best_score) {
            best_score = score;
            best = node;
        }
    }

    return best;
}

} // namespace control
