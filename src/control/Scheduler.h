#pragma once
#include "cdn_sim/EdgeNodePool.h"
#include "control/PolicyCenter.h"
#include "control/StreamTask.h"
#include <memory>
#include <string>

namespace control {

class Scheduler {
public:
    Scheduler(std::shared_ptr<cdn_sim::EdgeNodePool> pool,
              const SchedulerPolicy& policy);

    std::shared_ptr<cdn_sim::EdgeNode> SelectNode(
        const StreamTask& task,
        const std::string& exclude_node = "") const;

    const SchedulerPolicy& Policy() const { return policy_; }

private:
    std::shared_ptr<cdn_sim::EdgeNodePool> pool_;
    SchedulerPolicy policy_;
};

} // namespace control
