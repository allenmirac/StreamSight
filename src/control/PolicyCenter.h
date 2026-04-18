#pragma once

namespace control {

struct SchedulerPolicy {
    double region_weight     { 0.30 };
    double load_weight       { 0.30 };
    double capability_weight { 0.25 };
    double latency_weight    { 0.10 };
    double failover_weight   { 0.05 };

    int max_queue_threshold  { 32 };
    double busy_util_threshold { 0.90 };
};

} // namespace control
