// StreamApiServer.cpp
// Merged HTTP API server implementation.
// Includes all legacy routes from ai::HttpApiServer plus the new v1 session API.

// Include httplib before any system headers to avoid macro conflicts
#include "httplib.h"
#include "StreamApiServer.h"
#include "ai/FaceDatabase.h"
#include "ai/FaceRecognizer.h"
#include "observe/LatencyTracer.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

namespace api {

// ─── Pimpl to hide httplib types from the header ─────────────────────────────
struct StreamApiServer::Impl {
    httplib::Server svr;
};

// ─── JSON value extractors for POST body parsing ────────────────────────────
static std::string JsonGetValue(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' ||
            json[pos] == '\n' || json[pos] == '\r'))
        pos++;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // Extract quoted string
        size_t start = pos + 1;
        size_t end = start;
        while (end < json.size()) {
            if (json[end] == '\\') { end += 2; continue; }
            if (json[end] == '"') break;
            end++;
        }
        return json.substr(start, end - start);
    }

    if (json[pos] == '{' || json[pos] == '[') {
        // Extract nested object/array with bracket counting
        char open = json[pos];
        char close = (open == '{') ? '}' : ']';
        int depth = 1;
        size_t start = pos;
        pos++;
        while (pos < json.size() && depth > 0) {
            if (json[pos] == '"') {
                pos++;
                while (pos < json.size() && json[pos] != '"') {
                    if (json[pos] == '\\') pos++;
                    pos++;
                }
            } else if (json[pos] == open) {
                depth++;
            } else if (json[pos] == close) {
                depth--;
            }
            if (depth > 0) pos++;
        }
        return json.substr(start, pos - start + 1);
    }

    // Number, bool, or null literal
    size_t start = pos;
    while (pos < json.size() &&
           json[pos] != ',' && json[pos] != '}' &&
           json[pos] != ']' && json[pos] != ' ' &&
           json[pos] != '\t' && json[pos] != '\n' &&
           json[pos] != '\r') {
        pos++;
    }
    return json.substr(start, pos - start);
}

static std::string JsonGetString(const std::string& json, const std::string& key) {
    return JsonGetValue(json, key);
}

static int JsonGetInt(const std::string& json, const std::string& key, int def = 0) {
    std::string v = JsonGetValue(json, key);
    if (v.empty()) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}

static bool JsonGetBool(const std::string& json, const std::string& key, bool def = false) {
    std::string v = JsonGetValue(json, key);
    if (v == "true") return true;
    if (v == "false") return false;
    return def;
}

// ─── JSON helpers ─────────────────────────────────────────────────────────────
static std::string EscJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

static std::string FaceResultJson(const ai::FaceResult& f) {
    std::ostringstream ss;
    ss << "{"
       << "\"name\":\""     << EscJson(f.name) << "\","
       << "\"confidence\":" << std::fixed << std::setprecision(3) << f.confidence << ","
       << "\"similarity\":"  << std::fixed << std::setprecision(3) << f.similarity  << ","
       << "\"recognized\":"  << (f.recognized ? "true" : "false") << ","
       << "\"box\":{"
           << "\"x\":"      << f.box.x      << ","
           << "\"y\":"      << f.box.y      << ","
           << "\"width\":"  << f.box.width  << ","
           << "\"height\":" << f.box.height
       << "}}";
    return ss.str();
}

std::string StreamApiServer::ResultToJson(const ai::AnalysisResult& r) const {
    std::ostringstream ss;
    ss << "{\"timestamp_ms\":" << r.timestamp_ms
       << ",\"frame_id\":"     << r.frame_id
       << ",\"faces\":[";
    for (size_t i = 0; i < r.faces.size(); ++i) {
        if (i) ss << ',';
        ss << FaceResultJson(r.faces[i]);
    }
    ss << "]}";
    return ss.str();
}

std::string StreamApiServer::EventsToJson(int limit) const {
    std::lock_guard<std::mutex> lock(events_mutex_);
    std::ostringstream ss;
    ss << "[";
    int start = std::max(0, static_cast<int>(event_history_.size()) - limit);
    for (int i = start; i < static_cast<int>(event_history_.size()); ++i) {
        if (i > start) ss << ',';
        ss << ResultToJson(event_history_[i]);
    }
    ss << "]";
    return ss.str();
}

std::string StreamApiServer::StatusToJson() const {
    time_t now = ::time(nullptr);
    long uptime = static_cast<long>(now - start_time_);
    std::ostringstream ss;
    ss << "{"
       << "\"status\":\"running\","
       << "\"uptime_seconds\":" << uptime << ","
       << "\"port\":" << port_
       << "}";
    return ss.str();
}

std::string StreamApiServer::SessionStatusToJson(const ffmpeg::SessionStatus& s) {
    std::ostringstream ss;
    ss << "{"
       << "\"stream_id\":\"" << EscJson(s.stream_id) << "\","
       << "\"running\":" << (s.running ? "true" : "false") << ","
       << "\"frames_processed\":" << s.frames_processed << ","
       << "\"frames_dropped\":" << s.frames_dropped << ","
       << "\"uptime_seconds\":" << s.uptime_seconds << ","
       << "\"rtsp_port\":" << s.rtsp_port << ","
       << "\"http_port\":" << s.http_port;
    if (!s.error.empty()) {
        ss << ",\"error\":\"" << EscJson(s.error) << "\"";
    }
    ss << "}";
    return ss.str();
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────
StreamApiServer::StreamApiServer(int port, ai::FaceDatabase* db,
                                 ai::FaceRecognizer* recog, int max_events,
                                 ffmpeg::StreamServer* rtsp_server)
    : port_(port)
    , database_(db)
    , recognizer_(recog)
    , max_events_(max_events)
    , rtsp_server_(rtsp_server)
    , impl_(new Impl())
{
    start_time_ = ::time(nullptr);
}

StreamApiServer::~StreamApiServer() {
    Stop();
}

// ─── Route registration ───────────────────────────────────────────────────────
bool StreamApiServer::Start() {
    if (running_) return true;

    auto& svr = impl_->svr;

    // httplib default thread pool size is max(8, hardware_concurrency-1),
    // which creates 15+ idle threads on modern CPUs. For an embedded API
    // server that only serves occasional REST requests, 2 threads suffice.
    // The streaming pipeline runs on its own dedicated threads; httplib
    // only handles lightweight JSON serialization and route dispatching.
    svr.new_task_queue = []() -> httplib::TaskQueue* {
        return new httplib::ThreadPool(2);
    };

    // CORS
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Content-Type", "application/json"}
    });

    // ── Legacy routes (from ai::HttpApiServer) ────────────────────────────

    // GET /api/status
    svr.Get("/api/status", [this](const httplib::Request&,
                                   httplib::Response& res) {
        STREAMSIGHT_LATENCY_SCOPE("http", "http_request_total");
        res.set_content(StatusToJson(), "application/json");
    });

    // GET /api/current
    svr.Get("/api/current", [this](const httplib::Request&,
                                    httplib::Response& res) {
        STREAMSIGHT_LATENCY_SCOPE("http", "http_request_total");
        std::lock_guard<std::mutex> lock(result_mutex_);
        res.set_content(ResultToJson(current_result_), "application/json");
    });

    // GET /api/events?limit=N
    svr.Get("/api/events", [this](const httplib::Request& req,
                                   httplib::Response& res) {
        STREAMSIGHT_LATENCY_SCOPE("http", "http_request_total");
        int limit = 100;
        if (req.has_param("limit")) {
            try { limit = std::stoi(req.get_param_value("limit")); }
            catch (...) {}
        }
        res.set_content(EventsToJson(limit), "application/json");
    });

    // GET /api/faces
    svr.Get("/api/faces", [this](const httplib::Request&,
                                  httplib::Response& res) {
        STREAMSIGHT_LATENCY_SCOPE("http", "http_request_total");
        if (!database_) {
            res.status = 503;
            res.set_content("{\"error\":\"No database\"}", "application/json");
            return;
        }
        auto names = database_->ListNames();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < names.size(); ++i) {
            if (i) ss << ',';
            ss << "\"" << EscJson(names[i]) << "\"";
        }
        ss << "]";
        res.set_content(ss.str(), "application/json");
    });

    // POST /api/faces  (multipart form: "name" + "image" file)
    svr.Post("/api/faces", [this](const httplib::Request& req,
                                   httplib::Response& res) {
        STREAMSIGHT_LATENCY_SCOPE("http", "http_request_total");
        if (!database_ || !recognizer_) {
            res.status = 503;
            res.set_content("{\"error\":\"No database or recognizer\"}",
                             "application/json");
            return;
        }

        if (!req.form.has_field("name") || !req.form.has_file("image")) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing 'name' or 'image' field\"}",
                             "application/json");
            return;
        }

        std::string name = req.form.get_field("name");
        httplib::FormData imgfile = req.form.get_file("image");

        std::vector<uint8_t> data(imgfile.content.begin(),
                                   imgfile.content.end());
        cv::Mat img = cv::imdecode(data, cv::IMREAD_COLOR);
        if (img.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"Cannot decode image\"}", "application/json");
            return;
        }

        std::vector<float> emb = recognizer_->Extract(img);
        if (emb.empty()) {
            res.status = 422;
            res.set_content("{\"error\":\"Feature extraction failed\"}",
                             "application/json");
            return;
        }

        database_->Register(name, emb);
        database_->Save();

        std::ostringstream ss;
        ss << "{\"ok\":true,\"name\":\"" << EscJson(name) << "\"}";
        res.set_content(ss.str(), "application/json");
    });

    // DELETE /api/faces/{name}
    svr.Delete(R"(/api/faces/(.+))", [this](const httplib::Request& req,
                                             httplib::Response& res) {
        if (!database_) {
            res.status = 503;
            res.set_content("{\"error\":\"No database\"}", "application/json");
            return;
        }
        std::string name = req.matches[1];
        bool ok = database_->Remove(name);
        if (ok) database_->Save();
        std::ostringstream ss;
        ss << "{\"ok\":" << (ok ? "true" : "false")
           << ",\"name\":\"" << EscJson(name) << "\"}";
        res.set_content(ss.str(), "application/json");
    });

    // GET /api/latency/stats
    svr.Get("/api/latency/stats", [this](const httplib::Request&,
                                         httplib::Response& res) {
        auto stats = observe::LatencyTracer::Instance().GetStats();
        std::ostringstream ss;
        ss << "{";
        bool first = true;
        for (const auto& kv : stats) {
            if (!first) ss << ',';
            first = false;
            const auto& s = kv.second;
            std::vector<int64_t> d = s.durations;
            std::sort(d.begin(), d.end());
            auto pct = [&](double p) -> double {
                if (d.empty()) return 0;
                size_t idx = static_cast<size_t>(d.size() * p);
                if (idx >= d.size()) idx = d.size() - 1;
                return static_cast<double>(d[idx]) / 1000.0;
            };
            ss << "\"" << kv.first << "\":{"
               << "\"count\":" << s.count
               << ",\"avg_ms\":" << (s.count > 0 ? static_cast<double>(s.total_us) / s.count / 1000.0 : 0.0)
               << ",\"min_ms\":" << static_cast<double>(s.min_us) / 1000.0
               << ",\"max_ms\":" << static_cast<double>(s.max_us) / 1000.0
               << ",\"p50_ms\":" << pct(0.50)
               << ",\"p90_ms\":" << pct(0.90)
               << ",\"p95_ms\":" << pct(0.95)
               << ",\"p99_ms\":" << pct(0.99)
               << ",\"total_ms\":" << static_cast<double>(s.total_us) / 1000.0
               << "}";
        }
        ss << "}";
        res.set_content(ss.str(), "application/json");
    });

    // GET /api/latency/recent?limit=N
    svr.Get("/api/latency/recent", [this](const httplib::Request& req,
                                          httplib::Response& res) {
        int limit = 100;
        if (req.has_param("limit")) {
            try { limit = std::stoi(req.get_param_value("limit")); }
            catch (...) {}
        }
        auto events = observe::LatencyTracer::Instance().GetRecentEvents(limit);
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < events.size(); ++i) {
            if (i) ss << ',';
            const auto& ev = events[i];
            ss << "{"
               << "\"timestamp_ms\":" << ev.timestamp_ms
               << ",\"trace_id\":\"" << EscJson(ev.trace_id) << "\""
               << ",\"stream_id\":\"" << EscJson(ev.stream_id) << "\""
               << ",\"frame_id\":" << ev.frame_id
               << ",\"module\":\"" << EscJson(ev.module) << "\""
               << ",\"stage\":\"" << EscJson(ev.stage) << "\""
               << ",\"event\":\"" << EscJson(ev.event) << "\""
               << ",\"duration_us\":" << ev.duration_us
               << "}";
        }
        ss << "]";
        res.set_content(ss.str(), "application/json");
    });

    // POST /api/latency/reset
    svr.Post("/api/latency/reset", [this](const httplib::Request&,
                                          httplib::Response& res) {
        observe::LatencyTracer::Instance().Reset();
        res.set_content("{\"ok\":true}", "application/json");
    });

    // ── V1 Session routes ─────────────────────────────────────────────────

    // GET /api/v1/sessions — list session IDs
    svr.Get("/api/v1/sessions", [this](const httplib::Request&,
                                        httplib::Response& res) {
        auto ids = ListSessions();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i) ss << ',';
            ss << "\"" << EscJson(ids[i]) << "\"";
        }
        ss << "]";
        res.set_content(ss.str(), "application/json");
    });

    // POST /api/v1/sessions — create session from JSON body
    svr.Post("/api/v1/sessions", [this](const httplib::Request& req,
                                         httplib::Response& res) {
        ffmpeg::StreamSessionConfig cfg;
        const std::string& body = req.body;
        if (!body.empty()) {
            std::string v;
            v = JsonGetString(body, "input_url");
            if (!v.empty()) cfg.input_url = v;
            v = JsonGetString(body, "rtsp_suffix");
            if (!v.empty()) cfg.rtsp_suffix = v;
            v = JsonGetString(body, "rtmp_url");
            if (!v.empty()) cfg.rtmp_url = v;
            v = JsonGetString(body, "pipeline_mode");
            if (!v.empty()) cfg.pipeline_mode = v;
            v = JsonGetString(body, "effects_json");
            if (!v.empty()) cfg.effects_json = v;

            cfg.width = JsonGetInt(body, "width", 640);
            cfg.height = JsonGetInt(body, "height", 480);
            cfg.fps = JsonGetInt(body, "fps", 25);
            cfg.http_port = JsonGetInt(body, "http_port", 8080);
            cfg.bitrate = JsonGetInt(body, "bitrate", 2000000);
            cfg.enc_threads = JsonGetInt(body, "enc_threads", 2);
            cfg.ringbuf_size = JsonGetInt(body, "ringbuf_size", 4);
            cfg.max_frame_age_ms = JsonGetInt(body, "max_frame_age_ms", 500);
            cfg.time_window_ms = JsonGetInt(body, "time_window_ms", 0);
            cfg.analyze_fps = JsonGetInt(body, "analyze_fps", 5);

            cfg.enable_ai = JsonGetBool(body, "enable_ai", true);
            cfg.enable_audio = JsonGetBool(body, "enable_audio", true);
        }
        std::string id = CreateSession(cfg);
        std::ostringstream ss;
        ss << "{\"session_id\":\"" << EscJson(id) << "\"}";
        res.set_content(ss.str(), "application/json");
    });

    // DELETE /api/v1/sessions/(.+) — remove session
    svr.Delete(R"(/api/v1/sessions/(.+))", [this](const httplib::Request& req,
                                                    httplib::Response& res) {
        std::string id = req.matches[1];
        bool ok = RemoveSession(id);
        std::ostringstream ss;
        ss << "{\"ok\":" << (ok ? "true" : "false")
           << ",\"session_id\":\"" << EscJson(id) << "\"}";
        res.set_content(ss.str(), "application/json");
    });

    // GET /api/v1/sessions/(.+) — session status
    svr.Get(R"(/api/v1/sessions/(.+))", [this](const httplib::Request& req,
                                                 httplib::Response& res) {
        std::string id = req.matches[1];
        auto s = GetSessionStatus(id);
        res.set_content(SessionStatusToJson(s), "application/json");
    });

    // PUT /api/v1/sessions/(.+)/effects — update effects config
    svr.Put(R"(/api/v1/sessions/(.+)/effects)", [this](const httplib::Request& req,
                                                         httplib::Response& res) {
        std::string id = req.matches[1];
        auto session = GetSession(id);
        if (!session) {
            res.status = 404;
            res.set_content("{\"error\":\"Session not found\"}", "application/json");
            return;
        }
        bool ok = session->UpdateEffects(req.body);
        std::ostringstream ss;
        ss << "{\"ok\":" << (ok ? "true" : "false")
           << ",\"session_id\":\"" << EscJson(id) << "\"}";
        res.set_content(ss.str(), "application/json");
    });

    // GET /api/v1/sessions/(.+)/results — session results
    svr.Get(R"(/api/v1/sessions/(.+)/results)", [this](const httplib::Request& req,
                                                         httplib::Response& res) {
        std::string id = req.matches[1];
        auto session = GetSession(id);
        if (!session) {
            res.status = 404;
            res.set_content("{\"error\":\"Session not found\"}", "application/json");
            return;
        }
        auto status = session->GetStatus();
        auto effect_names = session->GetEffectNames();
        // Gather face recognition results if available
        std::string face_results_json = "null";
        auto face_plugin = session->GetFacePlugin();
        if (face_plugin) {
            face_results_json = "\"active\"";
        }
        std::ostringstream ss;
        ss << "{"
           << "\"session_id\":\"" << EscJson(id) << "\","
           << "\"status\":" << SessionStatusToJson(status) << ","
           << "\"effects\":[";
        for (size_t i = 0; i < effect_names.size(); ++i) {
            if (i) ss << ',';
            ss << "\"" << EscJson(effect_names[i]) << "\"";
        }
        ss << "],"
           << "\"face_plugin\":" << face_results_json
           << "}";
        res.set_content(ss.str(), "application/json");
    });

    running_ = true;
    server_thread_ = std::thread([this]() {
        std::cout << "[StreamApiServer] Listening on port " << port_ << std::endl;
        impl_->svr.listen("0.0.0.0", port_);
        running_ = false;
    });

    return true;
}

void StreamApiServer::Stop() {
    if (!running_) return;
    impl_->svr.stop();
    if (server_thread_.joinable()) server_thread_.join();
    running_ = false;
}

// ─── Session registry ─────────────────────────────────────────────────────────
std::string StreamApiServer::CreateSession(const ffmpeg::StreamSessionConfig& cfg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "sess_" + std::to_string(next_id_++);
    auto session = std::make_shared<ffmpeg::StreamSession>(cfg);
    if (rtsp_server_) {
        session->Start(rtsp_server_);
    }
    SessionEntry entry;
    entry.id = id;
    entry.session = session;
    sessions_[id] = entry;
    return id;
}

std::string StreamApiServer::RegisterSession(
        std::shared_ptr<ffmpeg::StreamSession> session) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "sess_" + std::to_string(next_id_++);
    SessionEntry entry;
    entry.id = id;
    entry.session = std::move(session);
    sessions_[id] = entry;
    return id;
}

bool StreamApiServer::RemoveSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return false;
    sessions_.erase(it);
    return true;
}

ffmpeg::SessionStatus StreamApiServer::GetSessionStatus(
        const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        ffmpeg::SessionStatus s;
        s.stream_id = session_id;
        s.error = "Session not found";
        return s;
    }
    auto status = it->second.session->GetStatus();
    status.stream_id = session_id;
    return status;
}

std::vector<std::string> StreamApiServer::ListSessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(sessions_.size());
    for (const auto& kv : sessions_) {
        ids.push_back(kv.first);
    }
    return ids;
}

std::shared_ptr<ffmpeg::StreamSession> StreamApiServer::GetSession(
        const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return nullptr;
    return it->second.session;
}

// ─── Legacy result / event API ────────────────────────────────────────────────
void StreamApiServer::UpdateResult(const ai::AnalysisResult& result) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    current_result_ = result;
}

void StreamApiServer::AddEvent(const ai::AnalysisResult& result) {
    if (result.faces.empty()) return;
    std::lock_guard<std::mutex> lock(events_mutex_);
    event_history_.push_back(result);
    while (static_cast<int>(event_history_.size()) > max_events_)
        event_history_.pop_front();
}

}  // namespace api