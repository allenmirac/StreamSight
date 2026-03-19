// HttpApiServer.h
// REST API server exposing face analysis results and face-database management.
// Built on cpp-httplib (header-only, zero extra dependencies).
//
// Thread safety:
//   - Start()/Stop() must be called from the main thread.
//   - Result updates (UpdateResult, AddEvent) are mutex-protected.
//   - Handler threads are managed by cpp-httplib internally.

#ifndef AI_HTTP_API_SERVER_H
#define AI_HTTP_API_SERVER_H

#include "FrameAnalyzer.h"
#include "FaceDatabase.h"
#include "FaceRecognizer.h"
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <deque>
#include <ctime>

namespace ai {

/**
 * @brief HTTP REST API server for the analysis pipeline.
 *
 * Endpoints:
 *   GET  /api/status          Server status + uptime
 *   GET  /api/current         Latest frame analysis result
 *   GET  /api/events?limit=N  Recent detection events
 *   GET  /api/faces           Registered face names
 *   POST /api/faces           Register face (multipart: name + image)
 *   DELETE /api/faces/{name}  Remove face
 *
 * Usage:
 *   HttpApiServer api(8080, &database, &recognizer);
 *   api.Start();
 *   api.UpdateResult(analysis_result);
 *   api.Stop();
 */
class HttpApiServer {
public:
    /**
     * @param port        TCP port to listen on.
     * @param database    Face database (may be nullptr for read-only mode).
     * @param recognizer  Face recognizer used for POST /api/faces.
     * @param max_events  Maximum events kept in memory (ring buffer).
     */
    HttpApiServer(int port,
                  FaceDatabase*    database,
                  FaceRecognizer*  recognizer,
                  int max_events = 1000);
    ~HttpApiServer();

    /** @brief Start the HTTP server in a background thread. */
    bool Start();

    /** @brief Stop the server and join the thread. */
    void Stop();

    /** @brief Push a new analysis result (called from analysis thread). */
    void UpdateResult(const AnalysisResult& result);

    /** @brief Append to event history (called from EventCallback). */
    void AddEvent(const AnalysisResult& result);

    bool IsRunning() const { return running_; }

private:
    int              port_;
    FaceDatabase*    database_;
    FaceRecognizer*  recognizer_;
    int              max_events_;
    bool             running_ = false;

    mutable std::mutex  result_mutex_;
    AnalysisResult      current_result_;

    mutable std::mutex          events_mutex_;
    std::deque<AnalysisResult>  event_history_;

    std::thread server_thread_;
    time_t      start_time_;

    // Pointer to httplib server (opaque to avoid including httplib.h in header)
    struct ServerImpl;
    std::unique_ptr<ServerImpl> impl_;

    // JSON serialization helpers
    std::string ResultToJson(const AnalysisResult& r) const;
    std::string EventsToJson(int limit) const;
    std::string StatusToJson() const;
};

} // namespace ai

#endif // AI_HTTP_API_SERVER_H
