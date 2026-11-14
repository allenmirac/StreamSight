#pragma once
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

namespace streamsight::observe {

class MetricsRegistry {
 public:
  static MetricsRegistry& Instance();

  void SetGauge(const std::string& scope, const std::string& name,
                double value);
  void IncCounter(const std::string& scope, const std::string& name,
                  double delta = 1.0);
  double GetMetric(const std::string& scope, const std::string& name) const;
  std::map<std::string, double> Snapshot(const std::string& prefix = "") const;
  // Remove every metric whose key starts with `prefix` (same prefix semantics
  // as Snapshot). Returns the number of keys removed. Used to drop
  // per-connection gauges when a connection goes away.
  size_t RemovePrefix(const std::string& prefix);

 private:
  std::string MakeKey(const std::string& scope, const std::string& name) const;

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, double> metrics_;
};

}  // namespace streamsight::observe
