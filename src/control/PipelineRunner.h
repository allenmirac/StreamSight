// PipelineRunner.h — LEGACY
// Uses the old fork+pipe H264Encoder path. Prefer ffmpeg::StreamPipeline
// for new development. Kept for reference and rtsp_edge_analysis_server.
//
// Executes a full AI analysis pipeline on an assigned edge node.
#pragma once
#include "control/StreamTask.h"
#include "observe/MetricsRegistry.h"
#include "xop/RtspServer.h"

#include <atomic>
#include <memory>
#include <string>

namespace ffmpeg {
class PipelineManager;
} // namespace ffmpeg

namespace control {

enum class PipelineExitCode {
	Completed,
	Failed,
	Stopped
};

struct PipelineResult {
	PipelineExitCode code { PipelineExitCode::Failed };
	std::string message;
	PipelineResult(PipelineExitCode c, const std::string& msg)
	    : code(c), message(msg) {}
};

class PipelineRunner {
public:
	PipelineRunner(const StreamTask& task,
	               xop::RtspServer* rtsp_server,
	               observe::MetricsRegistry* metrics,
	               const std::string& node_id);

	PipelineResult Run(std::atomic<bool>& stop_flag);

	// Set a shared PipelineManager for ffmpeg pipeline mode.
	// When set and task.use_ffmpeg_pipeline is true, Run() delegates to it.
	static void SetPipelineManager(ffmpeg::PipelineManager* mgr);

private:
	PipelineResult RunLegacy(std::atomic<bool>& stop_flag);
	PipelineResult RunFFmpeg(std::atomic<bool>& stop_flag);

	StreamTask task_;
	xop::RtspServer* rtsp_server_ { nullptr };
	observe::MetricsRegistry* metrics_ { nullptr };
	std::string node_id_;

	static ffmpeg::PipelineManager* s_pipeline_mgr_;
};

} // namespace control
