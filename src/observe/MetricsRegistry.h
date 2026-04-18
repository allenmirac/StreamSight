#pragma once
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

namespace observe {

class MetricsRegistry {
public:
    static MetricsRegistry& Instance();

    void SetGauge(const std::string& scope, const std::string& name, double value);
    void IncCounter(const std::string& scope, const std::string& name, double delta = 1.0);
    double GetMetric(const std::string& scope, const std::string& name) const;
    std::map<std::string, double> Snapshot(const std::string& prefix = "") const;

private:
    std::string MakeKey(const std::string& scope, const std::string& name) const;

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, double> metrics_;
};

} // namespace observe
