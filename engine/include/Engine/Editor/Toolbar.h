#pragma once

/**
 * @file Toolbar.h
 * @brief 工具栏 — 常用操作的快捷按钮条
 *
 * 包含：运行/停止、帧步进、Gizmo 切换、吸附、坐标空间、视口模式、Overlays。
 * 使用 ImGui 按钮绘制，可选集成 FontAwesome 图标字体。
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

        // ── 状态枚举 ──
        enum class PlayState : uint8 {
            Stopped,
            Playing,
            Paused
        };

        // ── 回调类型 ──
        using ActionCallback = std::function<void()>;
        using ViewModeChangeCallback = std::function<void(int newMode)>;
        using CameraSpeedCallback = std::function<void(float speed)>;

        // ── 播放控制回调 ──
        void SetPlayCallback(ActionCallback cb)     { m_PlayCallback = std::move(cb); }
        void SetStopCallback(ActionCallback cb)     { m_StopCallback = std::move(cb); }
        void SetPauseCallback(ActionCallback cb)    { m_PauseCallback = std::move(cb); }
        void SetStepCallback(ActionCallback cb)     { m_StepCallback = std::move(cb); }
        void SetResetLayoutCallback(ActionCallback cb) { m_ResetLayoutCallback = std::move(cb); }

        // ── Gizmo 回调 ──
        using GizmoModeCallback = std::function<void(int mode)>;
        using GizmoSpaceCallback = std::function<void(bool local)>;
        void SetGizmoModeCallback(GizmoModeCallback cb)   { m_GizmoModeCallback = std::move(cb); }
        void SetGizmoSpaceCallback(GizmoSpaceCallback cb) { m_GizmoSpaceCallback = std::move(cb); }

        /** 视口模式变更回调（通知渲染器切换 Wireframe/Solid/Lighting） */
        void SetViewModeCallback(ViewModeChangeCallback cb) { m_ViewModeCallback = std::move(cb); }

        // ── 吸附回调 ──
        void SetSnapCallback(ActionCallback cb)          { m_SnapCallback = std::move(cb); }
        void SetSnapValueCallback(std::function<void(float)> cb) { m_SnapValueCallback = std::move(cb); }

        // ── 相机速度回调 ──
        void SetCameraSpeedCallback(CameraSpeedCallback cb) { m_CameraSpeedCallback = std::move(cb); }

        // ── Overlay 回调 ──
        void SetGridToggleCallback(ActionCallback cb)     { m_GridToggleCallback = std::move(cb); }
        void SetGizmosToggleCallback(ActionCallback cb)   { m_GizmosToggleCallback = std::move(cb); }
        void SetCollidersToggleCallback(ActionCallback cb){ m_CollidersToggleCallback = std::move(cb); }

        // ── 状态查询/设置 ──
        PlayState GetPlayState() const { return m_PlayState; }
        void SetPlayState(PlayState state) { m_PlayState = state; }

        int GetGizmoMode() const { return m_GizmoMode; }
        void SetGizmoMode(int mode);
        bool IsGizmoLocal() const { return m_GizmoLocal; }
        void SetGizmoLocal(bool local);

        int GetViewMode() const { return m_ViewMode; }
        void SetViewMode(int mode) { m_ViewMode = mode; }

        bool IsSnapEnabled() const { return m_SnapEnabled; }
        void SetSnapEnabled(bool enabled) { m_SnapEnabled = enabled; }
        float GetSnapValue() const { return m_SnapValue; }
        void SetSnapValue(float val) { m_SnapValue = val; }

        float GetCameraSpeed() const { return m_CameraSpeed; }
        void SetCameraSpeed(float speed) { m_CameraSpeed = speed; }

        bool IsShowGrid() const { return m_ShowGrid; }
        void SetShowGrid(bool show) { m_ShowGrid = show; }
        bool IsShowGizmos() const { return m_ShowGizmos; }
        void SetShowGizmos(bool show) { m_ShowGizmos = show; }
        bool IsShowColliders() const { return m_ShowColliders; }
        void SetShowColliders(bool show) { m_ShowColliders = show; }

        // ── 渲染 ──
        void OnImGui();

    private:
        // ── 内部绘制方法（按功能分组） ──
        void DrawPlayGroup();
        void DrawTransformGroup();
        void DrawSnappingGroup();
        void DrawViewSettingsGroup();

        // ── 状态 ──
        PlayState m_PlayState = PlayState::Stopped;
        int m_GizmoMode = 0;        // 0=Translate, 1=Rotate, 2=Scale
        bool m_GizmoLocal = false;
        int m_ViewMode = 0;         // 0=Solid, 1=Wireframe, 2=Lighting

        bool m_SnapEnabled = false;
        float m_SnapValue = 0.5f;

        float m_CameraSpeed = 1.0f;

        bool m_ShowGrid = true;
        bool m_ShowGizmos = true;
        bool m_ShowColliders = false;

        // ── 回调 ──
        ActionCallback m_PlayCallback;
        ActionCallback m_StopCallback;
        ActionCallback m_PauseCallback;
        ActionCallback m_StepCallback;
        ActionCallback m_ResetLayoutCallback;
        ViewModeChangeCallback m_ViewModeCallback;

        GizmoModeCallback  m_GizmoModeCallback;
        GizmoSpaceCallback m_GizmoSpaceCallback;

        ActionCallback             m_SnapCallback;
        std::function<void(float)> m_SnapValueCallback;
        CameraSpeedCallback        m_CameraSpeedCallback;

        ActionCallback m_GridToggleCallback;
        ActionCallback m_GizmosToggleCallback;
        ActionCallback m_CollidersToggleCallback;
    };

} // namespace Engine