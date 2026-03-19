// FaceDatabase.cpp
// Simple JSON serialization without external library dependencies.

#include "FaceDatabase.h"
#include "FaceRecognizer.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace ai {

FaceDatabase::FaceDatabase(const std::string& db_path, float threshold)
    : db_path_(db_path), threshold_(threshold)
{}

// ─── Minimal hand-rolled JSON helpers ─────────────────────────────────────

static std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else { out += c; }
    }
    return out;
}

static std::string SerializeEntry(const FaceEntry& e) {
    std::ostringstream ss;
    ss << "{\"name\":\"" << JsonEscape(e.name) << "\",\"embedding\":[";
    for (size_t i = 0; i < e.embedding.size(); ++i) {
        if (i) ss << ',';
        ss << e.embedding[i];
    }
    ss << "]}";
    return ss.str();
}

// Very small JSON parser sufficient for our format
static bool ParseFloat(const std::string& s, size_t& pos,
                       std::vector<float>& out) {
    // Read array of floats: [..., ..., ...]
    while (pos < s.size() && s[pos] != '[') ++pos;
    if (pos >= s.size()) return false;
    ++pos; // skip '['
    while (pos < s.size() && s[pos] != ']') {
        while (pos < s.size() && (s[pos]==' '||s[pos]==','||s[pos]=='\n')) ++pos;
        if (pos >= s.size() || s[pos] == ']') break;
        char* endp;
        float v = std::strtof(s.c_str() + pos, &endp);
        if (endp == s.c_str() + pos) break;
        out.push_back(v);
        pos = (size_t)(endp - s.c_str());
    }
    return !out.empty();
}

static std::string ParseString(const std::string& s, size_t& pos) {
    while (pos < s.size() && s[pos] != '"') ++pos;
    if (pos >= s.size()) return {};
    ++pos; // skip opening "
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos+1 < s.size()) {
            result += s[pos+1];
            pos += 2;
        } else {
            result += s[pos++];
        }
    }
    ++pos; // skip closing "
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────

int FaceDatabase::Load() {
    std::ifstream f(db_path_);
    if (!f.is_open()) return 0;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();

    // Parse array of objects
    size_t pos = 0;
    int count = 0;
    while (pos < content.size()) {
        size_t ob = content.find('{', pos);
        if (ob == std::string::npos) break;

        // Find matching '}'
        int depth = 0;
        size_t cb = ob;
        for (; cb < content.size(); ++cb) {
            if (content[cb] == '{') ++depth;
            else if (content[cb] == '}') { if (--depth == 0) break; }
        }
        if (cb >= content.size()) break;

        std::string obj = content.substr(ob, cb - ob + 1);
        pos = cb + 1;

        // Parse "name" field
        size_t np = obj.find("\"name\"");
        if (np == std::string::npos) continue;
        np += 6; // skip "name"
        std::string name = ParseString(obj, np);
        if (name.empty()) continue;

        // Parse "embedding" field
        size_t ep = obj.find("\"embedding\"");
        if (ep == std::string::npos) continue;
        ep += 11;
        std::vector<float> emb;
        ParseFloat(obj, ep, emb);
        if (emb.empty()) continue;

        entries_.push_back({name, emb});
        ++count;
    }
    return count;
}

bool FaceDatabase::Save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream f(db_path_);
    if (!f.is_open()) return false;

    f << "[\n";
    for (size_t i = 0; i < entries_.size(); ++i) {
        f << "  " << SerializeEntry(entries_[i]);
        if (i + 1 < entries_.size()) f << ',';
        f << '\n';
    }
    f << "]\n";
    return true;
}

void FaceDatabase::Register(const std::string& name,
                             const std::vector<float>& embedding) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : entries_) {
        if (e.name == name) {
            e.embedding = embedding;
            return;
        }
    }
    entries_.push_back({name, embedding});
}

bool FaceDatabase::Remove(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::remove_if(entries_.begin(), entries_.end(),
                              [&](const FaceEntry& e) { return e.name == name; });
    if (it == entries_.end()) return false;
    entries_.erase(it, entries_.end());
    return true;
}

FaceMatch FaceDatabase::Query(const std::vector<float>& embedding) const {
    std::lock_guard<std::mutex> lock(mutex_);
    FaceMatch best{"unknown", -1.0f, false};
    for (const auto& e : entries_) {
        float sim = FaceRecognizer::Similarity(e.embedding, embedding);
        if (sim > best.similarity) {
            best.similarity = sim;
            best.name       = e.name;
        }
    }
    best.matched = (best.similarity >= threshold_);
    if (!best.matched) best.name = "unknown";
    return best;
}

std::vector<std::string> FaceDatabase::ListNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const auto& e : entries_) names.push_back(e.name);
    return names;
}

size_t FaceDatabase::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

} // namespace ai
