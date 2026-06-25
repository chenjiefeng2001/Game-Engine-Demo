#pragma once

/**
 * @file ProfilerPanel.h
 * @brief 工业级性能分析器面板 — 火焰图 + 时间轴 + 多维度监控
 *
 * 功能：
 *   - CPU / GPU / Memory / Physics 多维度时间轴
 *   - 帧捕获（冻结帧供逐帧分析）
 *   - 性能预警标记
 *   - 数据导出
 */

#include "Engine/Types.h"
#include "Engine/Debug/ProfilerCore.h"
#include <string>
#include <vector>

namespace Engine {

    class ProfilerPanel {
    public:
        ProfilerPanel();
        ~ProfilerPanel() = default;

        void OnImGui(Debug::ProfilerCore& profiler);

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        void SetFrameTime(float ms) { m_CurrentFrameTime = ms; }
        void SetDrawCalls(uint32 dc) { m_DrawCalls = dc; }
        void SetVertexCount(uint32 vc) { m_VertexCount = vc; }

    private:
        void DrawCPUTimeline(Debug::ProfilerCore& profiler);
        void DrawMemoryChart(Debug::ProfilerCore& profiler);
        void DrawCaptureControls(Debug::ProfilerCore& profiler);
        void DrawAggregatedStats(const Debug::ProfilerCore::AggregatedStats& stats);

        bool m_Visible = true;
        float m_CurrentFrameTime = 0.0f;
        uint32 m_DrawCalls = 0;
        uint32 m_VertexCount = 0;
        int m_SelectedModule = 0;
        bool m_ShowFlameGraph = true;
    };

} // namespace Engine