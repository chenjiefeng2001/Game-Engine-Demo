#pragma once

/**
 * @file StatusBar.h
 * @brief 编辑器状态栏 — 实时指标 + 后台任务 + Git 分支 + 快捷开关
 *
 * 常驻显示于编辑器底部的条形区域，包含：
 *   - FPS / 帧耗时（颜色编码：绿 < 16ms, 黄 < 33ms, 红 > 33ms）
 *   - 内存占用
 *   - 后台任务进度
 *   - Git 分支 / 版本
 *   - 快速开关（线框模式、静音等）
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace Engine {

    // ============================================================
    // 后台任务描述
    // ============================================================
    struct BackgroundTask {
        std::string name;
        std::string description;
        float       progress    = 0.0f;   // 0-1
        bool        isIndeterminate = false;
        bool        cancellable = false;
        std::function<void()> onCancel;

        bool IsValid() const { return !name.empty(); }
    };

    // ============================================================
    // 状态栏面板
    // ============================================================
    class StatusBar {
    public:
        StatusBar() = default;
        ~StatusBar() = default;

        StatusBar(const StatusBar&) = delete;
        StatusBar& operator=(const StatusBar&) = delete;

        void OnImGui();

        // ── 帧性能指标 ──
        void SetFrameTime(float ms) { m_FrameTimeMs = ms; }
        void SetFPS(float fps) { m_FPS = fps; }
        void SetMemoryUsage(uint64 usedMB, uint64 totalMB) {
            m_UsedMemMB = usedMB; m_TotalMemMB = totalMB;
        }
        void SetDrawCalls(uint32 dc) { m_DrawCalls = dc; }
        void SetTriangleCount(uint32 tris) { m_Triangles = tris; }

        // ── 环境信息 ──
        void SetGitBranch(const std::string& branch) { m_GitBranch = branch; }
        void SetTargetPlatform(const std::string& platform) { m_TargetPlatform = platform; }
        void SetEditorVersion(const std::string& version) { m_EditorVersion = version; }

        // ── 后台任务 ──
        void PushTask(const BackgroundTask& task);
        void UpdateTask(const std::string& name, float progress);
        void PopTask(const std::string& name);
        bool HasActiveTasks() const { return !m_ActiveTasks.empty(); }

        // ── 快捷开关 ──
        void SetQuickToggle(const std::string& name, bool* value,
                            const std::string& tooltip = "") {
            m_QuickToggles.push_back({name, value, tooltip});
        }
        void ClearQuickToggles() { m_QuickToggles.clear(); }

        // ── 全局通知 ──
        void ShowNotification(const std::string& message, float duration = 3.0f);
        void UpdateNotifications(float dt);

        // ── 可见性 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        float GetHeight() const { return m_Height; }

    private:
        struct QuickToggle {
            std::string name;
            bool*       value = nullptr;
            std::string tooltip;
        };

        bool m_Visible = true;
        float m_Height = 24.0f;

        // 性能
        float m_FrameTimeMs = 0.0f;
        float m_FPS = 0.0f;
        uint64 m_UsedMemMB = 0;
        uint64 m_TotalMemMB = 0;
        uint32 m_DrawCalls = 0;
        uint32 m_Triangles = 0;

        // 环境
        std::string m_GitBranch = "master";
        std::string m_TargetPlatform = "Windows";
        std::string m_EditorVersion = "v0.1";

        // 后台任务
        std::vector<BackgroundTask> m_ActiveTasks;
        mutable std::mutex m_TaskMutex;

        // 快捷开关
        std::vector<QuickToggle> m_QuickToggles;

        // 通知
        struct Notification {
            std::string message;
            float timer = 0.0f;
            float duration = 3.0f;
        };
        std::vector<Notification> m_Notifications;
    };

} // namespace Engine