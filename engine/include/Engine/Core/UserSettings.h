#pragma once

/**
 * @file UserSettings.h
 * @brief 用户设置 — 编辑器布局、快捷键、UI 偏好等
 *
 * 使用 Config 系统管理，内置不可变默认模板。
 * 用户设置通常保存到 "config/user_settings.json"。
 */

#include "Engine/Core/Config.h"
#include "Engine/Types.h"
#include <string>
#include <vector>

namespace Engine {

    class UserSettings {
    public:
        UserSettings();
        ~UserSettings() = default;

        UserSettings(const UserSettings&) = delete;
        UserSettings& operator=(const UserSettings&) = delete;

        // ── 生命周期 ──

        /** 加载设置（若文件不存在则创建默认模板并保存） */
        bool Load(const std::string& filepath);

        /** 保存当前设置到文件 */
        bool Save(const std::string& filepath) const;

        /** 恢复所有设置为默认值 */
        void RestoreDefaults();

        // ── 编辑器设置 ──

        bool   IsEditorVisible() const;
        void   SetEditorVisible(bool visible);

        float  GetUIScale() const;
        void   SetUIScale(float scale);

        bool   IsPerformanceOverlayVisible() const;
        void   SetPerformanceOverlayVisible(bool visible);

        // ── 最近打开文件 ──

        std::vector<std::string> GetRecentFiles() const;
        void     SetRecentFiles(const std::vector<std::string>& files);
        void     AddRecentFile(const std::string& filepath);

        // ── 快捷键 ──

        std::string GetKeyBinding(const std::string& action) const;
        void        SetKeyBinding(const std::string& action, const std::string& key);

    private:
        Config m_Config;
        Config m_Defaults;

        void BuildDefaults();
    };

} // namespace Engine
