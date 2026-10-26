#ifndef STREAMSIGHT_API_JSON_UTIL_H
#define STREAMSIGHT_API_JSON_UTIL_H

#include <string>

namespace streamsight::api {

// Extracts the raw value of a top-level key. Returns "" if absent.
std::string JsonGetValue(const std::string& json, const std::string& key);

std::string JsonGetString(const std::string& json, const std::string& key);

int JsonGetInt(const std::string& json, const std::string& key, int def = 0);

bool JsonGetBool(const std::string& json, const std::string& key,
                 bool def = false);

std::string EscJson(const std::string& s);

}  // namespace streamsight::api

#endif  // STREAMSIGHT_API_JSON_UTIL_H
