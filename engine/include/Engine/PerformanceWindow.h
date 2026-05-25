#pragma once

/**
 * @file PerformanceWindow.h
 * @brief 性能监控窗口 — FPS 折线图、帧时间、DrawCall、物理体数等统计
 *
 * 在 Application::OnImGui() 中调用 OnImGui() 即可显示。
 * 每帧调用 FeedStats() 传入统计数据。
 */

#include "Engine/Types.h"

namespace Engine {

    class PerformanceWindow {
    public:
        PerformanceWindow();
        ~PerformanceWindow() = default;

        // 禁止拷贝
        PerformanceWindow(const PerformanceWindow&) = delete;
        PerformanceWindow& operator=(const PerformanceWindow&) = delete;

        // ── 帧统计输入 ──

        /** 每帧传入最新统计数据 */
        void FeedStats(float32 frameTimeMs,
                       uint32 drawCallCount,
                       uint32 physicsBodyCount,
                       uint32 physicsJointCount,
                       uint32 activeAudioSources,
                       uint32 textureCount);

        // ── 渲染 ──

        /** 在 OnImGui() 中调用，绘制性能窗口 */
        void OnImGui();

        // ── 配置 ──

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const        { return m_Visible; }
        void ToggleVisibility()       { m_Visible = !m_Visible; }

    private:
        // ── FPS 历史环形缓冲区（用于折线图） ──
        static constexpr uint32 kHistorySize = 150;
        float32 m_FpsHistory[kHistorySize] = {};
        uint32  m_HistoryIndex = 0;
        uint32  m_HistoryCount = 0;  

        // ── 当前帧统计 ──
        float32 m_CurrentFPS = 0.0f;
        float32 m_FrameTimeMs = 0.0f;
        uint32  m_DrawCallCount = 0;
        uint32  m_PhysicsBodyCount = 0;
        uint32  m_PhysicsJointCount = 0;
        uint32  m_ActiveAudioSources = 0;
        uint32  m_TextureCount = 0;

        // ── 窗口状态 ──
        bool m_Visible = true;
    };

} // namespace Engine
