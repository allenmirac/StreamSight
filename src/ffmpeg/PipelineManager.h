// PipelineManager.h
// Multi-stream registry managing StreamPipeline instances.
// Maps stream_id → StreamPipeline for isolated per-stream processing.

#ifndef FFMPEG_PIPELINE_MANAGER_H
#define FFMPEG_PIPELINE_MANAGER_H

#include "StreamPipeline.h"
#include "observe/MetricsRegistry.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ffmpeg {

struct PipelineStats {
	int64_t frames_decoded       = 0;
	int64_t frames_dropped_demux = 0;
	int64_t frames_dropped_ai    = 0;
	int64_t frames_pruned_demux  = 0;
	int64_t frames_pruned_ai     = 0;
	int  decode_ring_fill    = 0;
	int  process_ring_fill   = 0;
	bool running             = false;
};

class PipelineManager {
public:
	explicit PipelineManager(observe::MetricsRegistry* metrics = nullptr);

	// Add a stream with the given config. Returns false if stream_id already exists.
	bool AddStream(const std::string& stream_id, const PipelineConfig& cfg);

	// Remove and stop a stream. Returns false if not found.
	bool RemoveStream(const std::string& stream_id);

	// Get stats for a stream. Returns empty stats if not found.
	PipelineStats GetStats(const std::string& stream_id) const;

	// Stop all streams.
	void StopAll();

	// Number of active streams.
	size_t ActiveStreams() const;

private:
	mutable std::mutex mutex_;
	std::unordered_map<std::string, std::unique_ptr<StreamPipeline>> pipelines_;
	observe::MetricsRegistry* metrics_;
};

} // namespace ffmpeg

#endif // FFMPEG_PIPELINE_MANAGER_H
