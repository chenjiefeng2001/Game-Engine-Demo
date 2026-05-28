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
        using StateCallback  = std::function<void(PlayState)>;

        void SetPlayCallback(ActionCallback cb)     { m_PlayCallback = std::move(cb); }
        void SetStopCallback(ActionCallback cb)     { m_StopCallback = std::move(cb); }
        void SetPauseCallback(ActionCallback cb)    { m_PauseCallback = std::move(cb); }
        void SetStepCallback(ActionCallback cb)     { m_StepCallback = std::move(cb); }
        void SetResetLayoutCallback(ActionCallback cb) { m_ResetLayoutCallback = std::move(cb); }

        // ── 状态查询 ──
        PlayState GetPlayState() const { return m_PlayState; }
        void SetPlayState(PlayState state) { m_PlayState = state; }

        // ── 渲染 ──
        /** 在 OnImGui() 中调用，绘制工具栏 */
        void OnImGui();

    private:
        PlayState m_PlayState = PlayState::Stopped;
        ActionCallback m_PlayCallback;
        ActionCallback m_StopCallback;
        ActionCallback m_PauseCallback;
        ActionCallback m_StepCallback;
        ActionCallback m_ResetLayoutCallback;
    };

} // namespace Engine
