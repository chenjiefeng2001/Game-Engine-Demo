#pragma once

/**
 * @file Toolbar.h
 * @brief 工具栏 — 播放传送带 + 渲染模式（P1-c 摘除手术后保留面）
 *
 * P1-c 裁决（GP1-DX 审计 §3.3 "整条摆设" + §7 OBS-T1）：gizmo 工具/吸附/
 * 坐标空间/Overlays/相机速度/Reset Layout 等控件全仓零消费者或与 ViewportPanel
 * 浮层重复，且 W/E/R 全局键在 Play 态与游戏 WASD 输入冲突 —— 全部摘除，
 * 不做状态桥接：真实 gizmo 状态以 ViewportPanel 浮层 + Q/W/E/R 为单一真相源，
 * 不提供虚假可供性（GP-DX-007 / GP-DX-008 同一纪律）。
 * 保留面 = 播放控制（真实结果驱动，S1 非乐观）+ Render Mode（直达渲染器）。
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
        /// Play 动作返回 false = 播放未启动（无场景/克隆失败等），
        /// 工具栏据实反映状态，不再乐观置位（GP1-DX S1）
        using PlayAction = std::function<bool()>;
        using ViewModeChangeCallback = std::function<void(int newMode)>;

        // ── 播放控制回调 ──
        void SetPlayCallback(PlayAction cb)         { m_PlayCallback = std::move(cb); }
        void SetStopCallback(ActionCallback cb)     { m_StopCallback = std::move(cb); }
        void SetPauseCallback(ActionCallback cb)    { m_PauseCallback = std::move(cb); }
        void SetStepCallback(ActionCallback cb)     { m_StepCallback = std::move(cb); }

        /** 视口模式变更回调（通知渲染器切换 Wireframe/Solid/Lighting） */
        void SetViewModeCallback(ViewModeChangeCallback cb) { m_ViewModeCallback = std::move(cb); }

        // ── 状态查询/设置 ──
        PlayState GetPlayState() const { return m_PlayState; }
        void SetPlayState(PlayState state) { m_PlayState = state; }

        int GetViewMode() const { return m_ViewMode; }
        void SetViewMode(int mode) { m_ViewMode = mode; }

        // ── 渲染 ──
        void OnImGui();

    private:
        // ── 内部绘制方法 ──
        void DrawPlayGroup();
        void DrawRenderModeGroup();

        // ── 状态 ──
        PlayState m_PlayState = PlayState::Stopped;
        int m_ViewMode = 0;         // 0=Solid, 1=Wireframe, 2=Lighting

        // ── 回调 ──
        PlayAction m_PlayCallback;
        ActionCallback m_StopCallback;
        ActionCallback m_PauseCallback;
        ActionCallback m_StepCallback;
        ViewModeChangeCallback m_ViewModeCallback;
    };

} // namespace Engine
