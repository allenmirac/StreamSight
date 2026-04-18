#pragma once
#include "cdn_sim/EdgeNode.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cdn_sim {

class EdgeNodePool {
public:
    void AddNode(const std::shared_ptr<EdgeNode>& node);
    std::vector<std::shared_ptr<EdgeNode>> AllNodes() const;
    std::shared_ptr<EdgeNode> GetNode(const std::string& node_id) const;

private:
    mutable std::mutex mu_;
    std::vector<std::shared_ptr<EdgeNode>> nodes_;
};

} // namespace cdn_sim
