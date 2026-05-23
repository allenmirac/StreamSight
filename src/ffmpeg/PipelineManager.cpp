// PipelineManager.cpp

#include "PipelineManager.h"
#include <iostream>

namespace ffmpeg {

PipelineManager::PipelineManager(observe::MetricsRegistry* metrics)
	: metrics_(metrics)
{}

bool PipelineManager::AddStream(const std::string& stream_id,
                                 const PipelineConfig& cfg) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (pipelines_.find(stream_id) != pipelines_.end()) {
		std::cerr << "[PipelineManager] stream " << stream_id
		          << " already exists" << std::endl;
		return false;
	}

	auto pipeline = std::unique_ptr<StreamPipeline>(
		new StreamPipeline(stream_id, cfg));
	if (!pipeline->Start()) {
		std::cerr << "[PipelineManager] failed to start " << stream_id << std::endl;
		return false;
	}

	pipelines_[stream_id] = std::move(pipeline);
	std::cout << "[PipelineManager] stream " << stream_id
	          << " added (" << pipelines_.size() << " active)" << std::endl;
	return true;
}

bool PipelineManager::RemoveStream(const std::string& stream_id) {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = pipelines_.find(stream_id);
	if (it == pipelines_.end()) {
		return false;
	}
	it->second->Stop();
	pipelines_.erase(it);
	std::cout << "[PipelineManager] stream " << stream_id
	          << " removed (" << pipelines_.size() << " active)" << std::endl;
	return true;
}

PipelineStats PipelineManager::GetStats(const std::string& stream_id) const {
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = pipelines_.find(stream_id);
	if (it == pipelines_.end()) {
		return PipelineStats{};
	}
	auto* p = it->second.get();
	PipelineStats s;
	s.frames_decoded       = p->FramesDecoded();
	s.frames_dropped_demux = p->FramesDroppedDemux();
	s.frames_dropped_ai    = p->FramesDroppedAI();
	s.frames_pruned_demux  = p->FramesPrunedDemux();
	s.frames_pruned_ai     = p->FramesPrunedAI();
	s.decode_ring_fill     = p->DecodeRingFill();
	s.process_ring_fill    = p->ProcessRingFill();
	s.running              = p->IsRunning();
	return s;
}

void PipelineManager::StopAll() {
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& pair : pipelines_) {
		pair.second->Stop();
	}
	pipelines_.clear();
	std::cout << "[PipelineManager] all streams stopped" << std::endl;
}

size_t PipelineManager::ActiveStreams() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return pipelines_.size();
}

} // namespace ffmpeg
