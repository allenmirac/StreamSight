#include "JsonUtil.h"

#include <cctype>

namespace streamsight::api {
namespace {

constexpr size_t kNpos = std::string::npos;

// Finds the value start (just after ':') of a key that belongs directly to
// the root object. Nested keys are skipped.
size_t FindTopLevelValueStart(const std::string& json, const std::string& key) {
  size_t pos = json.find('{');
  if (pos == kNpos) return kNpos;

  int depth = 0;
  while (pos < json.size()) {
    const char c = json[pos];
    if (c == '"') {
      size_t end = pos + 1;
      std::string token;
      while (end < json.size()) {
        if (json[end] == '\\' && end + 1 < json.size()) {
          token += json[end + 1];
          end += 2;
          continue;
        }
        if (json[end] == '"') break;
        token += json[end++];
      }
      if (end >= json.size()) return kNpos;

      size_t colon = end + 1;
      while (colon < json.size() &&
             std::isspace(static_cast<unsigned char>(json[colon]))) {
        ++colon;
      }
      if (depth == 1 && token == key && colon < json.size() &&
          json[colon] == ':') {
        ++colon;
        while (colon < json.size() &&
               std::isspace(static_cast<unsigned char>(json[colon]))) {
          ++colon;
        }
        return colon;
      }
      pos = end + 1;
      continue;
    }

    if (c == '{' || c == '[') {
      ++depth;
    } else if (c == '}' || c == ']') {
      --depth;
      if (depth <= 0) return kNpos;
    }
    ++pos;
  }
  return kNpos;
}

}  // namespace

std::string JsonGetValue(const std::string& json, const std::string& key) {
  size_t pos = FindTopLevelValueStart(json, key);
  if (pos == kNpos || pos >= json.size()) return "";

  if (json[pos] == '"') {
    const size_t start = ++pos;
    while (pos < json.size()) {
      if (json[pos] == '\\' && pos + 1 < json.size()) {
        pos += 2;
        continue;
      }
      if (json[pos] == '"') break;
      ++pos;
    }
    return json.substr(start, pos - start);
  }

  if (json[pos] == '{' || json[pos] == '[') {
    const char open = json[pos];
    const char close = (open == '{') ? '}' : ']';
    int depth = 1;
    const size_t start = pos;
    size_t scan = pos + 1;
    while (scan < json.size() && depth > 0) {
      const char c = json[scan];
      if (c == '"') {
        ++scan;
        while (scan < json.size() && json[scan] != '"') {
          if (json[scan] == '\\') ++scan;
          ++scan;
        }
      } else if (c == open) {
        ++depth;
      } else if (c == close) {
        --depth;
      }
      if (depth > 0) ++scan;
    }
    return json.substr(start, scan - start + 1);
  }

  const size_t start = pos;
  while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
         json[pos] != ']' &&
         !std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  return json.substr(start, pos - start);
}

std::string JsonGetString(const std::string& json, const std::string& key) {
  return JsonGetValue(json, key);
}

int JsonGetInt(const std::string& json, const std::string& key, int def) {
  const std::string value = JsonGetValue(json, key);
  if (value.empty()) return def;
  try {
    return std::stoi(value);
  } catch (...) {
    return def;
  }
}

bool JsonGetBool(const std::string& json, const std::string& key, bool def) {
  const size_t pos = FindTopLevelValueStart(json, key);
  if (pos == kNpos || pos >= json.size()) return def;
  if (json[pos] == '"') return def;  // quoted strings are not booleans

  const std::string value = JsonGetValue(json, key);
  if (value == "true") return true;
  if (value == "false") return false;
  return def;
}

std::string EscJson(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace streamsight::api
