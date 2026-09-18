// StreamApiServer.h
// Merged HTTP API server that combines the ported single-file result/event
// routes and the v1 session management API into a single server on a single port.
//
// Thread safety:
//   - Start()/Stop() must be called from the main thread.
//   - Result updates (UpdateResult, AddEvent) are mutex-protected.
//   - Session registry methods are mutex-protected.
//   - Handler threads are managed by cpp-httplib internally.

#ifndef STREAMSIGHT_API_STREAM_API_SERVER_H
#define STREAMSIGHT_API_STREAM_API_SERVER_H

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

namespace streamsight::ai {
class FaceDatabase;
class FaceRecognizer;
}  // namespace streamsight::ai

namespace streamsight::ffmpeg {
class StreamServer;
}  // namespace streamsight::ffmpeg

namespace streamsight::api {

class StreamApiServer {
public:
    StreamApiServer(int port,
                    streamsight::ai::FaceDatabase*   db = nullptr,
                    streamsight::ai::FaceRecognizer* recog = nullptr,
                    int max_events = 1000,
                    streamsight::ffmpeg::StreamServer* rtsp_server = nullptr);
    ~StreamApiServer();

    // Non-copyable
    StreamApiServer(const StreamApiServer&) = delete;
    StreamApiServer& operator=(const StreamApiServer&) = delete;

    bool Start();
    void Stop();
    bool IsRunning() const { return running_; }

    // Session registry
    std::string CreateSession(const streamsight::ffmpeg::StreamSessionConfig& cfg);
    std::string RegisterSession(std::shared_ptr<streamsight::ffmpeg::StreamSession> session);
    bool RemoveSession(const std::string& session_id);
    streamsight::ffmpeg::SessionStatus GetSessionStatus(const std::string& session_id) const;
    std::vector<std::string> ListSessions() const;
    std::shared_ptr<streamsight::ffmpeg::StreamSession> GetSession(const std::string& id) const;

    // Legacy result/event API (ported, see StreamApiServer.cpp)
    void UpdateResult(const streamsight::ai::AnalysisResult& result);
    void AddEvent(const streamsight::ai::AnalysisResult& result);

private:
    struct SessionEntry {
        std::string id;
        std::shared_ptr<streamsight::ffmpeg::StreamSession> session;
    };

    int  port_;
    streamsight::ai::FaceDatabase*   database_;
    streamsight::ai::FaceRecognizer* recognizer_;
    int  max_events_;
    streamsight::ffmpeg::StreamServer* rtsp_server_ = nullptr;
    bool running_ = false;
    std::thread server_thread_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionEntry> sessions_;
    int next_id_ = 1;

    // Legacy result/event state
    mutable std::mutex          result_mutex_;
    streamsight::ai::AnalysisResult          current_result_;
    mutable std::mutex          events_mutex_;
    std::deque<streamsight::ai::AnalysisResult> event_history_;
    time_t                      start_time_;

    struct Impl;
    std::unique_ptr<Impl> impl_;

    // JSON helpers
    std::string ResultToJson(const streamsight::ai::AnalysisResult& r) const;
    std::string EventsToJson(int limit) const;
    std::string StatusToJson() const;
    static std::string SessionStatusToJson(const streamsight::ffmpeg::SessionStatus& s);
};

}  // namespace streamsight::api

#endif  // STREAMSIGHT_API_STREAM_API_SERVER_H