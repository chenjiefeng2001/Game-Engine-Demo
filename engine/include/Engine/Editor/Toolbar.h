#pragma once

/**
 * @file Toolbar.h
 * @brief 工具栏 — 常用操作的快捷按钮条
 *
 * 包含：运行/停止、帧步进、布局重置等。
 * 使用 ImGui 按钮绘制，无外部依赖。
 */

#include "Engine/Types.h"
#include <functional>

namespace Engine {

    class Toolbar {
    public:
        Toolbar() = default;
        ~Toolbar() = default;

        Toolbar(const Toolbar&) = delete;
        Toolbar& operator=(const Toolbar&) = delete;

        // ── 状态 ──
        enum class PlayState : uint8 {
            Stopped,
            Playing,
            Paused
        };

        // ── 回调 ──
        using ActionCallback = std::function<void()>;
        using ViewModeChangeCallback = std::function<void(int newMode)>;

        void SetPlayCallback(ActionCallback cb)     { m_PlayCallback = std::move(cb); }
        void SetStopCallback(ActionCallback cb)     { m_StopCallback = std::move(cb); }
        void SetPauseCallback(ActionCallback cb)    { m_PauseCallback = std::move(cb); }
        void SetStepCallback(ActionCallback cb)     { m_StepCallback = std::move(cb); }
        void SetResetLayoutCallback(ActionCallback cb) { m_ResetLayoutCallback = std::move(cb); }

        /** 视口模式变更回调（通知渲染器切换 Wireframe/Solid/Lighting） */
        void SetViewModeCallback(ViewModeChangeCallback cb) { m_ViewModeCallback = std::move(cb); }

        // ── 状态查询 ──
        PlayState GetPlayState() const { return m_PlayState; }
        void SetPlayState(PlayState state) { m_PlayState = state; }

        // ── Gizmo 控制 ──
        int GetGizmoMode() const { return m_GizmoMode; }
        void SetGizmoMode(int mode) { m_GizmoMode = mode; }
        bool IsGizmoLocal() const { return m_GizmoLocal; }
        void SetGizmoLocal(bool local) { m_GizmoLocal = local; }

        // ── 视口模式 ──
        int GetViewMode() const { return m_ViewMode; }
        void SetViewMode(int mode) { m_ViewMode = mode; }

        // ── 渲染 ──
        void OnImGui();

    private:
        PlayState m_PlayState = PlayState::Stopped;
        int m_GizmoMode = 0;    // 0=Translate, 1=Rotate, 2=Scale
        bool m_GizmoLocal = false;
        int m_ViewMode = 0;     // 0=Solid, 1=Wireframe, 2=Lighting

        ActionCallback m_PlayCallback;
        ActionCallback m_StopCallback;
        ActionCallback m_PauseCallback;
        ActionCallback m_StepCallback;
        ActionCallback m_ResetLayoutCallback;
        ViewModeChangeCallback m_ViewModeCallback;
    };

} // namespace Engine
