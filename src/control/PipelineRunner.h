#pragma once
#include "control/StreamTask.h"
#include "observe/MetricsRegistry.h"
#include "xop/RtspServer.h"

#include <atomic>
#include <memory>
#include <string>

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

private:
    StreamTask task_;
    xop::RtspServer* rtsp_server_ { nullptr };
    observe::MetricsRegistry* metrics_ { nullptr };
    std::string node_id_;
};

} // namespace control
