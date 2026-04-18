#include "MetricsRegistry.h"

namespace observe {

MetricsRegistry& MetricsRegistry::Instance() {
    static MetricsRegistry inst;
    return inst;
}

std::string MetricsRegistry::MakeKey(const std::string& scope, const std::string& name) const {
    return scope + "." + name;
}

void MetricsRegistry::SetGauge(const std::string& scope, const std::string& name, double value) {
    std::lock_guard<std::mutex> lk(mu_);
    metrics_[MakeKey(scope, name)] = value;
}

void MetricsRegistry::IncCounter(const std::string& scope, const std::string& name, double delta) {
    std::lock_guard<std::mutex> lk(mu_);
    metrics_[MakeKey(scope, name)] += delta;
}

double MetricsRegistry::GetMetric(const std::string& scope, const std::string& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = metrics_.find(MakeKey(scope, name));
    return it == metrics_.end() ? 0.0 : it->second;
}

std::map<std::string, double> MetricsRegistry::Snapshot(const std::string& prefix) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::map<std::string, double> out;
    for (const auto& kv : metrics_) {
        if (prefix.empty() || kv.first.find(prefix) == 0) {
            out[kv.first] = kv.second;
        }
    }
    return out;
}

} // namespace observe
