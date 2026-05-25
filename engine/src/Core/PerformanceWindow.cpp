#include "Engine/PerformanceWindow.h"

#include <imgui.h>
#include <cmath>
#include <cstdio>

namespace Engine {

    PerformanceWindow::PerformanceWindow()
    {
        // 预填充 FPS 历史为 0
        for (uint32 i = 0; i < kHistorySize; ++i)
            m_FpsHistory[i] = 0.0f;
    }

    void PerformanceWindow::FeedStats(float32 frameTimeMs,
                                       uint32 drawCallCount,
                                       uint32 physicsBodyCount,
                                       uint32 physicsJointCount,
                                       uint32 activeAudioSources,
                                       uint32 textureCount)
    {
        // 计算当前 FPS
        m_FrameTimeMs = frameTimeMs;
        m_CurrentFPS = (frameTimeMs > 0.0f) ? 1000.0f / frameTimeMs : 0.0f;
        m_DrawCallCount = drawCallCount;
        m_PhysicsBodyCount = physicsBodyCount;
        m_PhysicsJointCount = physicsJointCount;
        m_ActiveAudioSources = activeAudioSources;
        m_TextureCount = textureCount;

        // 写入 FPS 历史环形缓冲区
        m_FpsHistory[m_HistoryIndex] = m_CurrentFPS;
        m_HistoryIndex = (m_HistoryIndex + 1) % kHistorySize;
        if (m_HistoryCount < kHistorySize)
            ++m_HistoryCount;
    }

    void PerformanceWindow::OnImGui()
    {
        if (!m_Visible)
            return;

        // ── 主性能窗口 ─────────────────────────────────────
        ImGui::SetNextWindowSize(ImVec2(380, 320), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);

        ImGui::Begin("Performance", &m_Visible, ImGuiWindowFlags_NoResize);

        // ── 实时数值面板 ──
        if (ImGui::BeginTable("Stats", 3, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextRow();

            // FPS
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("FPS");
            ImGui::TableSetColumnIndex(1);
            char fpsLabel[32];
            std::snprintf(fpsLabel, sizeof(fpsLabel), "%.1f", m_CurrentFPS);
            ImGui::TextColored(m_CurrentFPS >= 30.0f ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1),
                               "%s", fpsLabel);

            // Frame Time
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("Frame");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Frame Time");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f ms", m_FrameTimeMs);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("(%.3f s)", m_FrameTimeMs / 1000.0f);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Draw Calls");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", m_DrawCallCount);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Physics Bodies");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", m_PhysicsBodyCount);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("Joints: %u", m_PhysicsJointCount);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Audio Sources");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", m_ActiveAudioSources);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Textures");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", m_TextureCount);

            ImGui::EndTable();
        }

        ImGui::Separator();

        // ── FPS 折线图 ──
        // 使用 ImGui::PlotLines 绘制历史走势
        const float minFps = 0.0f;
        const float maxFps = 120.0f;
        const float plotHeight = 80.0f;

        // 提取有效数据（从最旧到最新）
        // 使用栈上数组，PlotLines 需要连续内存
        float plotData[kHistorySize];
        uint32 dataCount = (m_HistoryCount < kHistorySize) ? m_HistoryCount : kHistorySize;
        if (dataCount > 0)
        {
            // 重新排列：从最旧到最新
            if (m_HistoryCount < kHistorySize)
            {
                // 尚未填满，直接复制
                for (uint32 i = 0; i < dataCount; ++i)
                    plotData[i] = m_FpsHistory[i];
            }
            else
            {
                // 环形缓冲区已满，从 m_HistoryIndex 处开始是最旧数据
                for (uint32 i = 0; i < kHistorySize; ++i)
                    plotData[i] = m_FpsHistory[(m_HistoryIndex + i) % kHistorySize];
            }

            // 计算平均值和中位数用于标注
            float sum = 0.0f;
            float minVal = 9999.0f, maxVal = 0.0f;
            for (uint32 i = 0; i < dataCount; ++i)
            {
                sum += plotData[i];
                if (plotData[i] < minVal) minVal = plotData[i];
                if (plotData[i] > maxVal) maxVal = plotData[i];
            }
            float avg = sum / dataCount;

            ImGui::Text("FPS History (last %u frames)", dataCount);
            ImGui::PlotLines("##FPS", plotData, dataCount,
                             0, nullptr, minFps, maxFps,
                             ImVec2(ImGui::GetContentRegionAvail().x, plotHeight));

            // 统计摘要
            ImGui::Text("Avg: %.1f  Min: %.1f  Max: %.1f",
                        avg, minVal, maxVal);
        }
        else
        {
            ImGui::Text("Collecting data...");
        }

        ImGui::Separator();

        // ── 帧时间柱状图 ──
        {
            const float msMax = 33.33f;  // ~30 FPS 上限
            const float msVal = (m_FrameTimeMs < msMax) ? m_FrameTimeMs : msMax;
            const float barWidth = ImGui::GetContentRegionAvail().x;
            const float barHeight = 20.0f;

            ImGui::Text("Frame Time: %.1f ms", m_FrameTimeMs);

            // 自定义进度条风格
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 barMax = ImVec2(pos.x + barWidth, pos.y + barHeight);

            // 背景
            dl->AddRectFilled(pos, barMax, IM_COL32(40, 40, 40, 255));
            // 填充（归一化到 msMax）
            float fraction = msVal / msMax;
            ImVec2 fillMax = ImVec2(pos.x + barWidth * fraction, pos.y + barHeight);
            // 颜色：绿 < 16ms > 黄 > 红
            ImU32 color;
            if (m_FrameTimeMs < 16.0f)
                color = IM_COL32(0, 200, 80, 255);
            else if (m_FrameTimeMs < 33.0f)
                color = IM_COL32(220, 200, 0, 255);
            else
                color = IM_COL32(220, 40, 40, 255);
            dl->AddRectFilled(pos, fillMax, color);
            dl->AddRect(pos, barMax, IM_COL32(100, 100, 100, 255));

            // 标注文字
            char msLabel[32];
            std::snprintf(msLabel, sizeof(msLabel), "%.1f / %.0f ms", m_FrameTimeMs, msMax);
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 4, pos.y + 2));
            ImGui::Text("%s", msLabel);
            ImGui::Dummy(ImVec2(0, barHeight + 4));
        }

        ImGui::End();
    }

} // namespace Engine
