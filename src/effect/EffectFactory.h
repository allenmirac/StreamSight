// src/effect/EffectFactory.h
#ifndef EFFECT_EFFECT_FACTORY_H
#define EFFECT_EFFECT_FACTORY_H

#include "IEffectPlugin.h"
#include "FaceRecognitionPlugin.h"
#include <memory>
#include <string>
#include <sstream>
#include <iostream>
#include <functional>
#include <unordered_map>

namespace streamsight {

class EffectFactory {
public:
    using Creator = std::function<std::shared_ptr<IEffectPlugin>(const std::string& config_json)>;

    static std::shared_ptr<IEffectPlugin> Create(const std::string& name,
                                                  const std::string& config_json) {
        auto& registry = GetRegistry();
        auto it = registry.find(name);
        if (it == registry.end()) {
            std::cerr << "[EffectFactory] unknown plugin: " << name << std::endl;
            return nullptr;
        }
        return it->second(config_json);
    }

    static void Register(const std::string& name, Creator creator) {
        GetRegistry()[name] = std::move(creator);
    }

private:
    static std::unordered_map<std::string, Creator>& GetRegistry() {
        static std::unordered_map<std::string, Creator> registry;
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            RegisterFaceRecognition();
        }
        return registry;
    }

    static void RegisterFaceRecognition() {
        Register("FaceRecognition", [](const std::string& config_json) {
            FaceRecognitionPlugin::Config cfg;
            cfg.detect_model   = JsonGetString(config_json, "detect_model", "");
            cfg.recog_model    = JsonGetString(config_json, "recog_model", "");
            cfg.face_db_path   = JsonGetString(config_json, "face_db_path", "faces.json");
            cfg.event_log_path = JsonGetString(config_json, "event_log_path", "");
            cfg.analyze_fps    = JsonGetInt(config_json, "analyze_fps", 5);
            return std::make_shared<FaceRecognitionPlugin>(cfg);
        });
    }

    static std::string JsonGetString(const std::string& json,
                                      const std::string& key,
                                      const std::string& default_val) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return default_val;
        pos += search.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos < json.size() && json[pos] == '"') {
            pos++;
            size_t end = pos;
            while (end < json.size() && json[end] != '"') end++;
            return json.substr(pos, end - pos);
        }
        return default_val;
    }

    static int JsonGetInt(const std::string& json,
                           const std::string& key,
                           int default_val) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return default_val;
        pos += search.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos < json.size() && (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9'))) {
            try { return std::stoi(json.substr(pos)); }
            catch (...) { return default_val; }
        }
        return default_val;
    }
};

}  // namespace streamsight

#endif  // EFFECT_EFFECT_FACTORY_H
