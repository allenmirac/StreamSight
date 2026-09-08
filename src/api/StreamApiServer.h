// StreamApiServer.h
// Merged HTTP API server that combines legacy routes (from ai::HttpApiServer)
// and the new v1 session management API into a single server on a single port.
//
// Thread safety:
//   - Start()/Stop() must be called from the main thread.
//   - Result updates (UpdateResult, AddEvent) are mutex-protected.
//   - Session registry methods are mutex-protected.
//   - Handler threads are managed by cpp-httplib internally.

#ifndef API_STREAM_API_SERVER_H
#define API_STREAM_API_SERVER_H

#include "../ffmpeg/StreamSession.h"
#include "../ai/FrameAnalyzer.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <deque>
#include <cstdint>

namespace ai {
class FaceDatabase;
class FaceRecognizer;
}

namespace ffmpeg {
class StreamServer;
}

namespace api {

class StreamApiServer {
public:
    StreamApiServer(int port,
                    ai::FaceDatabase*   db = nullptr,
                    ai::FaceRecognizer* recog = nullptr,
                    int max_events = 1000,
                    ffmpeg::StreamServer* rtsp_server = nullptr);
    ~StreamApiServer();

    // Non-copyable
    StreamApiServer(const StreamApiServer&) = delete;
    StreamApiServer& operator=(const StreamApiServer&) = delete;

    bool Start();
    void Stop();
    bool IsRunning() const { return running_; }

    // Session registry
    std::string CreateSession(const ffmpeg::StreamSessionConfig& cfg);
    std::string RegisterSession(std::shared_ptr<ffmpeg::StreamSession> session);
    bool RemoveSession(const std::string& session_id);
    ffmpeg::SessionStatus GetSessionStatus(const std::string& session_id) const;
    std::vector<std::string> ListSessions() const;
    std::shared_ptr<ffmpeg::StreamSession> GetSession(const std::string& id) const;

    // Legacy result/event API (from HttpApiServer)
    void UpdateResult(const ai::AnalysisResult& result);
    void AddEvent(const ai::AnalysisResult& result);

private:
    struct SessionEntry {
        std::string id;
        std::shared_ptr<ffmpeg::StreamSession> session;
    };

    int  port_;
    ai::FaceDatabase*   database_;
    ai::FaceRecognizer* recognizer_;
    int  max_events_;
    ffmpeg::StreamServer* rtsp_server_ = nullptr;
    bool running_ = false;
    std::thread server_thread_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionEntry> sessions_;
    int next_id_ = 1;

    // Legacy result/event state
    mutable std::mutex          result_mutex_;
    ai::AnalysisResult          current_result_;
    mutable std::mutex          events_mutex_;
    std::deque<ai::AnalysisResult> event_history_;
    time_t                      start_time_;

    struct Impl;
    std::unique_ptr<Impl> impl_;

    // JSON helpers
    std::string ResultToJson(const ai::AnalysisResult& r) const;
    std::string EventsToJson(int limit) const;
    std::string StatusToJson() const;
    static std::string SessionStatusToJson(const ffmpeg::SessionStatus& s);
};

}  // namespace api

#endif  // API_STREAM_API_SERVER_H