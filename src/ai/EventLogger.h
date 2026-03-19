// EventLogger.h
// Writes face-detection events to a newline-delimited JSON log file.
// Each line is a standalone JSON object describing one analysis event.
//
// Thread safety: Log() is protected by an internal mutex.

#ifndef AI_EVENT_LOGGER_H
#define AI_EVENT_LOGGER_H

#include "FrameAnalyzer.h"
#include <string>
#include <mutex>
#include <fstream>

namespace ai {

/**
 * @brief Appends detection events to a JSON-lines log file.
 *
 * Log format (one JSON object per line):
 *   {"ts":1710000000000,"frame":42,"faces":[
 *     {"name":"Alice","conf":0.95,"sim":0.72,"x":100,"y":50,"w":80,"h":90},
 *     ...
 *   ]}
 *
 * Usage:
 *   EventLogger logger("events.jsonl");
 *   logger.Open();
 *   analyzer.SetEventCallback([&](const AnalysisResult& r) {
 *       logger.Log(r);
 *   });
 */
class EventLogger {
public:
    /**
     * @param log_path  Path to output .jsonl file (appended if exists).
     * @param min_faces Only write events with at least this many faces (default 1).
     */
    explicit EventLogger(const std::string& log_path = "events.jsonl",
                         int min_faces = 1);
    ~EventLogger();

    /** @brief Open log file for writing. */
    bool Open();

    /** @brief Close log file. */
    void Close();

    /**
     * @brief Append one analysis result to the log.
     * Skipped if no faces detected (when min_faces > 0).
     */
    void Log(const AnalysisResult& result);

private:
    std::string        log_path_;
    int                min_faces_;
    mutable std::mutex mutex_;
    std::ofstream      file_;
};

} // namespace ai

#endif // AI_EVENT_LOGGER_H
