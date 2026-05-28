#include "Engine/Core/UserSettings.h"
#include <iostream>

namespace Engine {

    UserSettings::UserSettings() {
        BuildDefaults();
        m_Config.SetDefaults(m_Defaults);
    }

    void UserSettings::BuildDefaults() {
        // ── Editor ──
        m_Defaults.SetBool("Editor", "visible", true);
        m_Defaults.SetFloat("Editor", "uiScale", 1.0f);
        m_Defaults.SetBool("Editor", "performanceOverlay", true);

        // ── Recent Files ──
        m_Defaults.SetString("Recent", "files", "[]");

        // ── Key Bindings ──
        m_Defaults.SetString("KeyBindings", "toggleUI",         "F1");
        m_Defaults.SetString("KeyBindings", "toggleConsole",    "F2");
        m_Defaults.SetString("KeyBindings", "saveScene",        "Ctrl+S");
        m_Defaults.SetString("KeyBindings", "play",             "F5");
        m_Defaults.SetString("KeyBindings", "stop",             "Shift+F5");
    }

    bool UserSettings::Load(const std::string& filepath) {
        bool loaded = m_Config.Load(filepath);
        if (!loaded) {
            RestoreDefaults();
            Save(filepath);
            std::cout << "[UserSettings] Created default: " << filepath << std::endl;
        }
        return true;
    }

    bool UserSettings::Save(const std::string& filepath) const {
        return m_Config.Save(filepath);
    }

    void UserSettings::RestoreDefaults() {
        m_Config.RestoreDefaults();
    }

    // ── Editor ──

    bool UserSettings::IsEditorVisible() const {
        return m_Config.GetBool("Editor", "visible", true);
    }

    void UserSettings::SetEditorVisible(bool visible) {
        m_Config.SetBool("Editor", "visible", visible);
    }

    float UserSettings::GetUIScale() const {
        return m_Config.GetFloat("Editor", "uiScale", 1.0f);
    }

    void UserSettings::SetUIScale(float scale) {
        m_Config.SetFloat("Editor", "uiScale", scale);
    }

    bool UserSettings::IsPerformanceOverlayVisible() const {
        return m_Config.GetBool("Editor", "performanceOverlay", true);
    }

    void UserSettings::SetPerformanceOverlayVisible(bool visible) {
        m_Config.SetBool("Editor", "performanceOverlay", visible);
    }

    // ── Recent Files ──

    std::vector<std::string> UserSettings::GetRecentFiles() const {
        std::vector<std::string> result;
        std::string raw = m_Config.GetString("Recent", "files", "[]");
        try {
            auto arr = nlohmann::json::parse(raw);
            for (auto& item : arr) {
                if (item.is_string())
                    result.push_back(item.get<std::string>());
            }
        } catch (...) {}
        return result;
    }

    void UserSettings::SetRecentFiles(const std::vector<std::string>& files) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& f : files)
            arr.push_back(f);
        m_Config.SetString("Recent", "files", arr.dump());
    }

    void UserSettings::AddRecentFile(const std::string& filepath) {
        auto files = GetRecentFiles();
        // 去重：如果已存在则先移除
        files.erase(std::remove(files.begin(), files.end(), filepath), files.end());
        // 插入到最前
        files.insert(files.begin(), filepath);
        // 最多保留 10 个
        if (files.size() > 10) files.resize(10);
        SetRecentFiles(files);
    }

    // ── Key Bindings ──

    std::string UserSettings::GetKeyBinding(const std::string& action) const {
        return m_Config.GetString("KeyBindings", action, "");
    }

    void UserSettings::SetKeyBinding(const std::string& action, const std::string& key) {
        m_Config.SetString("KeyBindings", action, key);
    }

} // namespace Engine
