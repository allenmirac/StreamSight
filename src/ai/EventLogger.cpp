// EventLogger.cpp

#include "EventLogger.h"
#include <sstream>
#include <iostream>
#include <iomanip>

namespace ai {

EventLogger::EventLogger(const std::string& log_path, int min_faces)
    : log_path_(log_path), min_faces_(min_faces)
{}

EventLogger::~EventLogger() {
    Close();
}

bool EventLogger::Open() {
    std::lock_guard<std::mutex> lock(mutex_);
    file_.open(log_path_, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "[EventLogger] Cannot open: " << log_path_ << std::endl;
        return false;
    }
    return true;
}

void EventLogger::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) file_.close();
}

static std::string EscapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

void EventLogger::Log(const AnalysisResult& result) {
    if ((int)result.faces.size() < min_faces_) return;

    std::ostringstream ss;
    ss << "{\"ts\":" << result.timestamp_ms
       << ",\"frame\":" << result.frame_id
       << ",\"faces\":[";

    for (size_t i = 0; i < result.faces.size(); ++i) {
        const auto& f = result.faces[i];
        if (i) ss << ',';
        ss << "{"
           << "\"name\":\"" << EscapeJson(f.name) << "\","
           << "\"conf\":" << std::fixed << std::setprecision(3) << f.confidence << ","
           << "\"sim\":"  << std::fixed << std::setprecision(3) << f.similarity  << ","
           << "\"x\":"    << f.box.x    << ","
           << "\"y\":"    << f.box.y    << ","
           << "\"w\":"    << f.box.width  << ","
           << "\"h\":"    << f.box.height
           << "}";
    }
    ss << "]}";

    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_ << ss.str() << '\n';
        file_.flush();
    }
}

} // namespace ai
