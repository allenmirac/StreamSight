#include "cdn_sim/EdgeNodePool.h"

namespace cdn_sim {

void EdgeNodePool::AddNode(const std::shared_ptr<EdgeNode>& node) {
    std::lock_guard<std::mutex> lk(mu_);
    nodes_.push_back(node);
}

std::vector<std::shared_ptr<EdgeNode>> EdgeNodePool::AllNodes() const {
    std::lock_guard<std::mutex> lk(mu_);
    return nodes_;
}

std::shared_ptr<EdgeNode> EdgeNodePool::GetNode(const std::string& node_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& n : nodes_) {
        if (n->Id() == node_id) return n;
    }
    return nullptr;
}

} // namespace cdn_sim
