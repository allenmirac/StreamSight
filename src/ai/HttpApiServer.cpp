// HttpApiServer.cpp

// Include httplib before any system headers to avoid macro conflicts
#include "httplib.h"
#include "HttpApiServer.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace ai {

// ─── Pimpl to hide httplib types from the header ─────────────────────────────
struct HttpApiServer::ServerImpl {
    httplib::Server svr;
};

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

static std::string FaceResultJson(const FaceResult& f) {
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

std::string HttpApiServer::ResultToJson(const AnalysisResult& r) const {
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

std::string HttpApiServer::EventsToJson(int limit) const {
    std::lock_guard<std::mutex> lock(events_mutex_);
    std::ostringstream ss;
    ss << "[";
    int start = std::max(0, (int)event_history_.size() - limit);
    for (int i = start; i < (int)event_history_.size(); ++i) {
        if (i > start) ss << ',';
        ss << ResultToJson(event_history_[i]);
    }
    ss << "]";
    return ss.str();
}

std::string HttpApiServer::StatusToJson() const {
    time_t now = ::time(nullptr);
    long uptime = (long)(now - start_time_);
    std::ostringstream ss;
    ss << "{"
       << "\"status\":\"running\","
       << "\"uptime_seconds\":" << uptime << ","
       << "\"port\":" << port_
       << "}";
    return ss.str();
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────
HttpApiServer::HttpApiServer(int port, FaceDatabase* database,
                               FaceRecognizer* recognizer, int max_events)
    : port_(port)
    , database_(database)
    , recognizer_(recognizer)
    , max_events_(max_events)
    , impl_(new ServerImpl())
{
    start_time_ = ::time(nullptr);
}

HttpApiServer::~HttpApiServer() {
    Stop();
}

// ─── Route registration ───────────────────────────────────────────────────────
bool HttpApiServer::Start() {
    if (running_) return true;

    auto& svr = impl_->svr;

    // CORS
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Content-Type", "application/json"}
    });

    // GET /api/status
    svr.Get("/api/status", [this](const httplib::Request&,
                                   httplib::Response& res) {
        res.set_content(StatusToJson(), "application/json");
    });

    // GET /api/current
    svr.Get("/api/current", [this](const httplib::Request&,
                                    httplib::Response& res) {
        std::lock_guard<std::mutex> lock(result_mutex_);
        res.set_content(ResultToJson(current_result_), "application/json");
    });

    // GET /api/events?limit=100
    svr.Get("/api/events", [this](const httplib::Request& req,
                                   httplib::Response& res) {
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
        if (!database_ || !recognizer_) {
            res.status = 503;
            res.set_content("{\"error\":\"No database or recognizer\"}",
                             "application/json");
            return;
        }

        // Get name field (text) and image file
        if (!req.form.has_field("name") || !req.form.has_file("image")) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing 'name' or 'image' field\"}",
                             "application/json");
            return;
        }

        std::string name    = req.form.get_field("name");
        httplib::FormData imgfile = req.form.get_file("image");

        // Decode image
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

    running_ = true;
    server_thread_ = std::thread([this]() {
        std::cout << "[HttpApiServer] Listening on port " << port_ << std::endl;
        impl_->svr.listen("0.0.0.0", port_);
        running_ = false;
    });

    return true;
}

void HttpApiServer::Stop() {
    if (!running_) return;
    impl_->svr.stop();
    if (server_thread_.joinable()) server_thread_.join();
    running_ = false;
}

void HttpApiServer::UpdateResult(const AnalysisResult& result) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    current_result_ = result;
}

void HttpApiServer::AddEvent(const AnalysisResult& result) {
    if (result.faces.empty()) return;
    std::lock_guard<std::mutex> lock(events_mutex_);
    event_history_.push_back(result);
    while ((int)event_history_.size() > max_events_)
        event_history_.pop_front();
}

} // namespace ai
