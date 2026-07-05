#include "Engine/Core/Config.h"
#include "Engine/Core/Log.h"
#include <fstream>
#include <sstream>

namespace {
    Engine::Logger s_Log("Config");
}

namespace Engine {

    // ============================================================
    // 持久化
    // ============================================================

    bool Config::Load(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            s_Log.Error("Failed to open: {}", filepath);
            return false;
        }

        try {
            nlohmann::json loaded;
            file >> loaded;
            if (!loaded.is_object()) {
                s_Log.Error("Invalid format (not an object): {}", filepath);
                return false;
            }

            // 合并加载的值到当前配置（不覆盖默认模板）
            for (auto it = loaded.begin(); it != loaded.end(); ++it) {
                m_Data[it.key()] = it.value();
            }

            s_Log.Info("Loaded: {}", filepath);
            return true;
        } catch (const nlohmann::json::exception& e) {
            s_Log.Error("JSON parse error in {}: {}", filepath, e.what());
            return false;
        }
    }

    bool Config::Save(const std::string& filepath) const {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            s_Log.Error("Failed to write: {}", filepath);
            return false;
        }

        try {
            file << m_Data.dump(4);  // 缩进 4 空格，人类可读
            s_Log.Info("Saved: {}", filepath);
            return true;
        } catch (const nlohmann::json::exception& e) {
            s_Log.Error("JSON serialize error: {}", e.what());
            return false;
        }
    }

    // ============================================================
    // 值访问 — 读
    // ============================================================

    int32 Config::GetInt(const std::string& section, const std::string& key,
                         int32 defaultValue) const {
        try {
            auto& sec = m_Data.at(section);
            if (sec.contains(key)) {
                auto& val = sec[key];
                if (val.is_number_integer()) return val.get<int32>();
                if (val.is_number_float())   return static_cast<int32>(val.get<float>());
                if (val.is_boolean())        return val.get<bool>() ? 1 : 0;
                if (val.is_string())         return std::stoi(val.get<std::string>());
            }
        } catch (...) {}
        return defaultValue;
    }

    float32 Config::GetFloat(const std::string& section, const std::string& key,
                             float32 defaultValue) const {
        try {
            auto& sec = m_Data.at(section);
            if (sec.contains(key)) {
                auto& val = sec[key];
                if (val.is_number_float())   return val.get<float32>();
                if (val.is_number_integer()) return static_cast<float32>(val.get<int32>());
                if (val.is_string())         return std::stof(val.get<std::string>());
            }
        } catch (...) {}
        return defaultValue;
    }

    bool Config::GetBool(const std::string& section, const std::string& key,
                         bool defaultValue) const {
        try {
            auto& sec = m_Data.at(section);
            if (sec.contains(key)) {
                auto& val = sec[key];
                if (val.is_boolean())        return val.get<bool>();
                if (val.is_number_integer()) return val.get<int32>() != 0;
                if (val.is_string()) {
                    auto s = val.get<std::string>();
                    return s == "true" || s == "1" || s == "yes";
                }
            }
        } catch (...) {}
        return defaultValue;
    }

    std::string Config::GetString(const std::string& section, const std::string& key,
                                  const std::string& defaultValue) const {
        try {
            auto& sec = m_Data.at(section);
            if (sec.contains(key)) {
                auto& val = sec[key];
                if (val.is_string()) return val.get<std::string>();
                return val.dump();
            }
        } catch (...) {}
        return defaultValue;
    }

    // ============================================================
    // 值访问 — 写
    // ============================================================

    nlohmann::json& Config::EnsureSection(const std::string& section) {
        if (!m_Data.contains(section)) {
            m_Data[section] = nlohmann::json::object();
        }
        return m_Data[section];
    }

    void Config::SetInt(const std::string& section, const std::string& key, int32 value) {
        EnsureSection(section)[key] = value;
    }

    void Config::SetFloat(const std::string& section, const std::string& key, float32 value) {
        EnsureSection(section)[key] = value;
    }

    void Config::SetBool(const std::string& section, const std::string& key, bool value) {
        EnsureSection(section)[key] = value;
    }

    void Config::SetString(const std::string& section, const std::string& key,
                           const std::string& value) {
        EnsureSection(section)[key] = value;
    }

    // ============================================================
    // 默认模板管理
    // ============================================================

    void Config::SetDefaults(const Config& defaults) {
        m_Defaults = defaults.m_Data;  // 深拷贝默认值
        m_HasDefaults = true;
    }

    void Config::RestoreDefaults() {
        if (!m_HasDefaults) {
            s_Log.Error("No defaults registered, cannot restore.");
            return;
        }
        // 将 m_Defaults 深拷贝到 m_Data
        m_Data = m_Defaults;
        s_Log.Info("Restored defaults");
    }

    Config Config::GetDiff() const {
        Config diff;
        if (!m_HasDefaults) return diff;

        for (auto it = m_Data.begin(); it != m_Data.end(); ++it) {
            const std::string& section = it.key();
            const auto& secData = it.value();

            if (!secData.is_object()) continue;

            for (auto kv = secData.begin(); kv != secData.end(); ++kv) {
                const std::string& key = kv.key();
                const auto& val = kv.value();

                // 检查与默认值是否相同
                bool isDefault = false;
                try {
                    if (m_Defaults.contains(section) &&
                        m_Defaults[section].contains(key) &&
                        m_Defaults[section][key] == val) {
                        isDefault = true;
                    }
                } catch (...) {}

                if (!isDefault) {
                    diff.m_Data[section][key] = val;
                }
            }
        }
        return diff;
    }

    // ============================================================
    // 查询
    // ============================================================

    bool Config::HasKey(const std::string& section, const std::string& key) const {
        try {
            return m_Data.at(section).contains(key);
        } catch (...) {
            return false;
        }
    }

    bool Config::HasSection(const std::string& section) const {
        return m_Data.contains(section);
    }

    std::vector<std::string> Config::GetSectionNames() const {
        std::vector<std::string> names;
        names.reserve(m_Data.size());
        for (auto it = m_Data.begin(); it != m_Data.end(); ++it) {
            names.push_back(it.key());
        }
        return names;
    }

    std::vector<std::string> Config::GetKeyNames(const std::string& section) const {
        std::vector<std::string> keys;
        try {
            auto& sec = m_Data.at(section);
            for (auto it = sec.begin(); it != sec.end(); ++it) {
                keys.push_back(it.key());
            }
        } catch (...) {}
        return keys;
    }

    void Config::Clear() {
        m_Data = nlohmann::json::object();
    }

} // namespace Engine
