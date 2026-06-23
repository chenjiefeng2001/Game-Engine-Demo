#include "Engine/PerformanceWindow.h"
#include "Engine/Core/IRenderContext.h"

#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace Engine {

    // ── 工具函数 ─────────────────────────────────────────────────

    /// 将字节数格式化为人类可读字符串 (B/KB/MB/GB)
    static void FormatBytes(char* buf, size_t bufSize, uint64 bytes) {
        static const char* units[] = {"B", "KB", "MB", "GB"};
        int unitIdx = 0;
        double val = static_cast<double>(bytes);
        while (val >= 1024.0 && unitIdx < 3) {
            val /= 1024.0;
            ++unitIdx;
        }
        std::snprintf(buf, bufSize, "%.2f %s", val, units[unitIdx]);
    }

    /// 在 ImDrawList 上绘制一个渐变色进度条
    static void DrawGradientBar(ImDrawList* dl, ImVec2 pos, ImVec2 size,
                                 float fraction, ImU32 colorFrom, ImU32 colorTo) {
        // 背景
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          IM_COL32(35, 35, 35, 255));
        // 填充
        float fillW = (std::min)(size.x * fraction, size.x);
        if (fillW > 1.0f) {
            ImVec2 fillMax(pos.x + fillW, pos.y + size.y);
            dl->AddRectFilledMultiColor(pos, fillMax, colorFrom, colorTo,
                                        colorTo, colorFrom);
        }
        // 边框
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                    IM_COL32(100, 100, 100, 180));
    }

    // ── 构造 ─────────────────────────────────────────────────────

    PerformanceWindow::PerformanceWindow() {
        for (uint32 i = 0; i < kHistorySize; ++i) {
            m_FpsHistory[i] = 0.0f;
            m_FrameTimeHistory[i] = 0.0f;
        }
    }

    // ── FeedStats ────────────────────────────────────────────────

    void PerformanceWindow::FeedStats(float32 frameTimeMs,
                                       uint32 drawCallCount,
                                       uint32 physicsBodyCount,
                                       uint32 physicsJointCount,
                                       uint32 activeAudioSources,
                                       uint32 textureCount) {
        m_FrameTimeMs = frameTimeMs;
        m_CurrentFPS  = (frameTimeMs > 0.0f) ? 1000.0f / frameTimeMs : 0.0f;
        m_DrawCallCount = drawCallCount;
        m_PhysicsBodyCount = physicsBodyCount;
        m_PhysicsJointCount = physicsJointCount;
        m_ActiveAudioSources = activeAudioSources;
        m_TextureCount = textureCount;

        // FPS 历史
        m_FpsHistory[m_HistoryIndex] = m_CurrentFPS;
        // Frame Time 历史
        m_FrameTimeHistory[m_HistoryIndex] = frameTimeMs;

        m_HistoryIndex = (m_HistoryIndex + 1) % kHistorySize;
        if (m_HistoryCount < kHistorySize)
            ++m_HistoryCount;
    }

    // ── OnImGui 主入口 ───────────────────────────────────────────

    void PerformanceWindow::OnImGui() {
        if (!m_Visible)
            return;

        // 窗口大小根据内容自动调整，稍大以容纳瀑布图
        ImGui::SetNextWindowSize(ImVec2(440, 580), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);

        ImGui::Begin("Performance", &m_Visible,
                     ImGuiWindowFlags_NoCollapse);

        // ── 1. FPS / Frame Time ──
        if (ImGui::CollapsingHeader("FPS / Frame Time", nullptr,
                                     ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawFPSFrameTime();
        }

        // ── 2. Draw Calls ──
        if (ImGui::CollapsingHeader("Draw Calls", nullptr,
                                     ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawDrawCalls();
        }

        // ── 3. 几何体统计 ──
        if (ImGui::CollapsingHeader("Geometry Counts", nullptr,
                                     ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawGeometryCounts();
        }

        // ── 4. 显存占用 ──
        if (ImGui::CollapsingHeader("VRAM Usage", nullptr,
                                     ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawVRAMUsage();
        }

        // ── 5. GPU Profiler 瀑布图 ──
        if (ImGui::CollapsingHeader("GPU Profiler", nullptr,
                                     ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawGPUProfiler();
        }

        ImGui::End();

        // 以下渲染调试功能已移至 Renderer Debug 面板
        //（在 EngineEditor::OnImGui() 中，仅在 Play 模式下显示）
        // 包括：View Modes / Buffer Visualization / Lighting & Shadows
        //       Geometry & Culling / Post-Processing / Textures & Assets
        //       Helper Toggles
    }

    // ── 1. FPS / Frame Time ──────────────────────────────────────

    void PerformanceWindow::DrawFPSFrameTime() {
        // ── 实时数值面板 (3 列: 指标名 | 数值 | 备注) ──
        if (ImGui::BeginTable("##FpsTable", 3,
                              ImGuiTableFlags_SizingStretchSame))
        {
            // 行 0: FPS
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("FPS");
            ImGui::TableSetColumnIndex(1);
            char fpsBuf[32];
            std::snprintf(fpsBuf, sizeof(fpsBuf), "%.1f", m_CurrentFPS);
            ImVec4 fpsColor = (m_CurrentFPS >= 58.0f) ? ImVec4(0,1,0,1)
                            : (m_CurrentFPS >= 30.0f) ? ImVec4(1,1,0,1)
                            : ImVec4(1,0,0,1);
            ImGui::TextColored(fpsColor, "%s", fpsBuf);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "target: 60");

            // 行 1: Total Frame Time
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Frame Time");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f ms", m_FrameTimeMs);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("(%.1f FPS)", m_CurrentFPS);

            // 行 2: CPU Time
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("CPU Time");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.3f,0.6f,1.0f,1), "%.2f ms", m_CPUTimeMs);
            ImGui::TableSetColumnIndex(2);
            float cpuRatio = (m_FrameTimeMs > 0.0f) ? (m_CPUTimeMs / m_FrameTimeMs) * 100.0f : 0.0f;
            ImGui::Text("(%.1f%%)", cpuRatio);

            // 行 3: GPU Time
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("GPU Time");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.0f,0.8f,0.4f,1), "%.2f ms", m_GPUTimeMs);
            ImGui::TableSetColumnIndex(2);
            float gpuRatio = (m_FrameTimeMs > 0.0f) ? (m_GPUTimeMs / m_FrameTimeMs) * 100.0f : 0.0f;
            ImGui::Text("(%.1f%%)", gpuRatio);

            ImGui::EndTable();
        }

        ImGui::Separator();

        // ── 帧时间横向堆叠条 (CPU vs GPU) ──
        {
            const float barMaxMs = 33.33f; // 30 FPS 红线
            const float barW = ImGui::GetContentRegionAvail().x;
            const float barH = 26.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();

            // 背景 (灰色)
            dl->AddRectFilled(pos, ImVec2(pos.x + barW, pos.y + barH),
                              IM_COL32(30,30,30,255));

            // CPU 耗时 (蓝)
            float cpuFrac = (std::min)(m_CPUTimeMs / barMaxMs, 1.0f);
            float cpuW = barW * cpuFrac;
            if (cpuW > 1.0f) {
                dl->AddRectFilled(pos, ImVec2(pos.x + cpuW, pos.y + barH),
                                  IM_COL32(50, 120, 220, 200));
            }

            // GPU 耗时 (绿) — 从 CPU 结束处开始
            float gpuFrac = (std::min)(m_GPUTimeMs / barMaxMs, 1.0f);
            float gpuW = barW * gpuFrac;
            if (cpuW + gpuW > 1.0f && cpuW + gpuW <= barW) {
                dl->AddRectFilled(ImVec2(pos.x + cpuW, pos.y),
                                  ImVec2(pos.x + cpuW + gpuW, pos.y + barH),
                                  IM_COL32(0, 180, 80, 200));
            } else if (gpuW > 1.0f) {
                // GPU 单独显示 (如果 CPU 占满)
                dl->AddRectFilled(pos, ImVec2(pos.x + gpuW, pos.y + barH),
                                  IM_COL32(0, 180, 80, 200));
            }

            // 边框
            dl->AddRect(pos, ImVec2(pos.x + barW, pos.y + barH),
                        IM_COL32(100,100,100,180));

            // 文字标注
            char barLabel[64];
            std::snprintf(barLabel, sizeof(barLabel),
                          "CPU: %.2f ms | GPU: %.2f ms  [max: %.0f ms]",
                          m_CPUTimeMs, m_GPUTimeMs, barMaxMs);
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 4, pos.y + 4));
            ImGui::Text("%s", barLabel);
            ImGui::Dummy(ImVec2(0, barH + 4));
        }

        // ── FPS 折线图 ──
        {
            const float minFps = 0.0f;
            const float maxFps = 120.0f;
            const float plotH = 80.0f;

            // 构建有序数据
            float plotData[kHistorySize];
            uint32 dataCount = (m_HistoryCount < kHistorySize) ? m_HistoryCount : kHistorySize;
            if (dataCount > 0) {
                if (m_HistoryCount < kHistorySize) {
                    for (uint32 i = 0; i < dataCount; ++i)
                        plotData[i] = m_FpsHistory[i];
                } else {
                    for (uint32 i = 0; i < kHistorySize; ++i)
                        plotData[i] = m_FpsHistory[(m_HistoryIndex + i) % kHistorySize];
                }

                // 统计
                float sum = 0.0f, minVal = 9999.0f, maxVal = 0.0f;
                for (uint32 i = 0; i < dataCount; ++i) {
                    sum += plotData[i];
                    if (plotData[i] < minVal) minVal = plotData[i];
                    if (plotData[i] > maxVal) maxVal = plotData[i];
                }
                float avg = sum / dataCount;

                ImGui::Text("FPS History (last %u frames)", dataCount);
                ImGui::PlotLines("##FPS", plotData, dataCount,
                                 0, nullptr, minFps, maxFps,
                                 ImVec2(ImGui::GetContentRegionAvail().x, plotH));

                ImGui::Text("Avg: %.1f  Min: %.1f  Max: %.1f",
                            avg, minVal, maxVal);
            } else {
                ImGui::Text("Collecting data...");
            }
        }
    }

    // ── 2. Draw Calls ────────────────────────────────────────────

    void PerformanceWindow::DrawDrawCalls() {
        ImVec2 avail = ImGui::GetContentRegionAvail();

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);

        // 大号 DrawCall 计数
        char dcBuf[32];
        std::snprintf(dcBuf, sizeof(dcBuf), "%u", m_DrawCallCount);
        ImGui::TextColored(ImVec4(1,1,0,1), "Draw Calls:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0,1,0.8f,1), "%s", dcBuf);

        ImGui::Spacing();

        // ── DrawCall 健康度指示器 ──
        // < 100: 绿色, 100~500: 黄色, > 500: 红色
        {
            const float warnLow  = 100.0f;
            const float warnHigh = 500.0f;
            float dcNorm = static_cast<float>(m_DrawCallCount);
            float fraction = 0.0f;
            if (dcNorm >= warnHigh)
                fraction = 1.0f;
            else if (dcNorm > warnLow)
                fraction = (dcNorm - warnLow) / (warnHigh - warnLow);
            else
                fraction = dcNorm / warnLow;

            // 缩放防止超出
            fraction = (std::min)(fraction, 1.0f);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float barW = ImGui::GetContentRegionAvail().x;
            float barH = 18.0f;

            ImU32 colFrom = IM_COL32(0, 200, 80, 255);
            ImU32 colTo   = IM_COL32(220, 40, 40, 255);

            DrawGradientBar(dl, pos, ImVec2(barW, barH), fraction, colFrom, colTo);

            char barLabel[64];
            std::snprintf(barLabel, sizeof(barLabel),
                          "DC: %u  [Low < %d | High > %d]",
                          m_DrawCallCount, (int)warnLow, (int)warnHigh);
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 4, pos.y + 1));
            ImGui::Text("%s", barLabel);
            ImGui::Dummy(ImVec2(0, barH + 4));
        }

        ImGui::Spacing();
        ImGui::Separator();

        // ── 额外统计 ──
        if (ImGui::BeginTable("##ExtraStats", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Textures Loaded");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", m_TextureCount);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Physics Bodies");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", m_PhysicsBodyCount);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Physics Joints");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", m_PhysicsJointCount);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Audio Sources");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", m_ActiveAudioSources);

            ImGui::EndTable();
        }
    }

    // ── 3. 三角形 / 顶点计数 ─────────────────────────────────────

    void PerformanceWindow::DrawGeometryCounts() {
        if (ImGui::BeginTable("##GeomTable", 2, ImGuiTableFlags_SizingStretchSame)) {
            // 顶点
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Vertices (this frame)");
            ImGui::TableSetColumnIndex(1);
            char vBuf[32];
            FormatBytes(vBuf, sizeof(vBuf), static_cast<uint64>(m_VertexCount));
            ImGui::TextColored(ImVec4(0.3f,0.8f,1.0f,1), "%u (%s)",
                               m_VertexCount, vBuf);

            // 三角形
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Triangles (this frame)");
            ImGui::TableSetColumnIndex(1);
            char tBuf[32];
            FormatBytes(tBuf, sizeof(tBuf), static_cast<uint64>(m_TriangleCount));
            ImGui::TextColored(ImVec4(0.3f,1.0f,0.5f,1), "%u (%s)",
                               m_TriangleCount, tBuf);

            ImGui::EndTable();
        }

        // ── 简单比例表 ──
        ImGui::Spacing();
        ImGui::Separator();

        // 推导：每个三角形 3 个顶点，但共享顶点会复用
        // 近似每个三角形贡献 3 个索引
        uint32 approxIndexCount = m_TriangleCount * 3;

        if (ImGui::BeginTable("##GeomDetail", 3, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Metric");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("Count");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("Est. Indices");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Triangles");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", m_TriangleCount);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", approxIndexCount);

            ImGui::EndTable();
        }
    }

    // ── 4. 显存占用 ──────────────────────────────────────────────

    void PerformanceWindow::DrawVRAMUsage() {
        uint64 totalVRAM = m_VRAMTextureBytes + m_VRAMBufferBytes + m_VRAMModelBytes;

        // 总计 (大号)
        char totalBuf[64];
        FormatBytes(totalBuf, sizeof(totalBuf), totalVRAM);
        ImGui::TextColored(ImVec4(1,1,1,1), "Total VRAM:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0,1,0.5f,1), "%s", totalBuf);

        ImGui::Spacing();

        // ── 堆叠条形图 ──
        {
            const float barMax = (std::max)(static_cast<uint64>(1), totalVRAM);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float barW = ImGui::GetContentRegionAvail().x;
            float barH = 30.0f;

            // 背景
            dl->AddRectFilled(pos, ImVec2(pos.x + barW, pos.y + barH),
                              IM_COL32(25,25,25,255));

            auto drawSegment = [&](uint64 bytes, ImU32 color, float& xOff) {
                if (bytes == 0) return;
                float frac = static_cast<float>(bytes) / static_cast<float>(barMax);
                float segW = barW * frac;
                if (segW < 1.0f) segW = 1.0f;
                dl->AddRectFilled(ImVec2(pos.x + xOff, pos.y),
                                  ImVec2(pos.x + xOff + segW, pos.y + barH),
                                  color);
                xOff += segW;
            };

            float offset = 0.0f;
            drawSegment(m_VRAMTextureBytes, IM_COL32(60,160,230,200), offset); // 蓝: 纹理
            drawSegment(m_VRAMBufferBytes,  IM_COL32(230,160,60,200), offset); // 橙: 缓冲
            drawSegment(m_VRAMModelBytes,   IM_COL32(160,80,200,200), offset); // 紫: 模型

            dl->AddRect(pos, ImVec2(pos.x + barW, pos.y + barH),
                        IM_COL32(100,100,100,180));

            ImGui::SetCursorScreenPos(ImVec2(pos.x + 4, pos.y + 6));
            ImGui::Text("Textures | Buffers | Models");
            ImGui::Dummy(ImVec2(0, barH + 6));
        }

        // ── 明细表 ──
        if (ImGui::BeginTable("##VRAMTable", 2, ImGuiTableFlags_SizingStretchSame)) {
            auto row = [](const char* label, uint64 bytes, ImVec4 color) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", label);
                ImGui::TableSetColumnIndex(1);
                char buf[32];
                FormatBytes(buf, sizeof(buf), bytes);
                ImGui::TextColored(color, "%s", buf);
            };

            row("Textures",       m_VRAMTextureBytes, ImVec4(0.3f,0.6f,1.0f,1));
            row("Buffers (VB/IB)", m_VRAMBufferBytes,  ImVec4(1.0f,0.7f,0.3f,1));
            row("Model Data",     m_VRAMModelBytes,   ImVec4(0.6f,0.3f,0.8f,1));
            ImGui::EndTable();
        }

        // ── 占位提示 ──
        if (totalVRAM == 0) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "No VRAM data. Implement GPU memory tracking\n"
                               "in your RHI layer to populate these values.");
        }
    }

    // ── 5. GPU Profiler 瀑布图（固定 Pass 表） ──────────────────

    DebugPassId PerformanceWindow::MapPassNameToId(const std::string& name) {
        // 简单的前缀匹配 —— 忽略大小写
        if (name == "ShadowPass" || name == "shadowpass")  return DebugPassId::ShadowPass;
        if (name == "BasePass"   || name == "basepass")     return DebugPassId::BasePass;
        if (name == "SSAO"       || name == "ssao")         return DebugPassId::SSAO;
        if (name == "Bloom"      || name == "bloom")        return DebugPassId::Bloom;
        if (name == "ToneMapping"|| name == "tonemapping")  return DebugPassId::ToneMapping;
        if (name == "FXAA"       || name == "fxaa")         return DebugPassId::FXAA;
        if (name == "PostProcess"|| name == "postprocess")  return DebugPassId::PostProcess;
        if (name == "UIPass"     || name == "uipass")       return DebugPassId::UIPass;
        // 未知 Pass 名 — 分配到 ShadowPass 位置但保持 inactive
        return DebugPassId::ShadowPass;
    }

    void PerformanceWindow::SetGPUProfiler(const GPUProfilerSnapshot& snap) {
        // 1. 先将所有条目标记为 non-active
        for (auto& p : m_StablePasses) {
            p.active = false;
            p.timeMs = 0.0f;
        }

        // 2. 将动态 Pass 数据映射到固定表
        for (uint32 i = 0; i < snap.passCount && i < GPUProfilerSnapshot::kMaxPasses; ++i) {
            DebugPassId id = MapPassNameToId(snap.passes[i].name);
            auto& entry = m_StablePasses[(int32)id];
            entry.active = true;
            entry.timeMs = snap.passes[i].timeMs;
        }
    }

    void PerformanceWindow::DrawGPUProfiler() {
        // ── 计算总耗时（仅统计 active pass） ──
        float totalGPUms = 0.0f;
        uint32 activeCount = 0;
        for (auto& p : m_StablePasses) {
            if (p.active) {
                totalGPUms += p.timeMs;
                ++activeCount;
            }
        }

        char totalGpuBuf[64];
        std::snprintf(totalGpuBuf, sizeof(totalGpuBuf),
                      "Total GPU: %.3f ms (%u active / %zu total)",
                      totalGPUms, activeCount, m_StablePasses.size());
        ImGui::TextColored(ImVec4(0,1,0.5f,1), "%s", totalGpuBuf);

        ImGui::Spacing();

        if (activeCount == 0) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "No GPU pass data yet.");
            return;
        }

        const float rowH = 26.0f;
        const float labelW = 140.0f;
        const float msBarMax = (std::max)(totalGPUms, 16.667f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float availW = ImGui::GetContentRegionAvail().x - labelW;
        if (availW < 50.0f) availW = 50.0f;

        // ── 表头 + 刻度尺 ──
        {
            float rulerY = ImGui::GetCursorScreenPos().y;
            ImVec2 rulerStart = ImGui::GetCursorScreenPos();
            ImGui::Text("Pass Name");
            ImGui::SameLine(labelW);
            ImGui::Text("Timeline (ms)");

            const int numTicks = 5;
            for (int t = 1; t <= numTicks; ++t) {
                float tickFrac = static_cast<float>(t) / static_cast<float>(numTicks);
                float tickX = rulerStart.x + availW * tickFrac;
                dl->AddLine(ImVec2(tickX, rulerY + 2),
                            ImVec2(tickX, rulerY + 10),
                            IM_COL32(100,100,100,150));
                char tickLabel[16];
                std::snprintf(tickLabel, sizeof(tickLabel), "%.1f", msBarMax * tickFrac);
                dl->AddText(ImVec2(tickX - 12, rulerY + 10),
                            IM_COL32(120,120,120,200), tickLabel);
            }
        }

        ImGui::Dummy(ImVec2(0, 4));

        // ── 固定颜色（按 DebugPassId 索引） ──
        static const ImU32 passColors[] = {
            IM_COL32(50,  180, 230, 220),  // 0  ShadowPass: 浅蓝
            IM_COL32(230, 180, 50,  220),  // 1  BasePass: 金色
            IM_COL32(80,  200, 120, 220),  // 2  SSAO: 绿色
            IM_COL32(200, 80,  120, 220),  // 3  Bloom: 玫红
            IM_COL32(120, 100, 220, 220),  // 4  ToneMapping: 淡紫
            IM_COL32(220, 120, 50,  220),  // 5  FXAA: 橙色
            IM_COL32(50,  200, 200, 220),  // 6  PostProcess: 青色
            IM_COL32(200, 200, 80,  220),  // 7  UIPass: 黄绿
        };

        // ── 遍历固定表绘制（行数恒定，窗口永不跳变） ──
        for (size_t i = 0; i < m_StablePasses.size(); ++i) {
            const auto& entry = m_StablePasses[i];
            const char* passName = kDebugPassNames[i];

            ImVec2 rowPos = ImGui::GetCursorScreenPos();
            float rowY0 = rowPos.y;
            float rowY1 = rowY0 + rowH;

            // 交替背景
            if (i % 2 == 1) {
                dl->AddRectFilled(ImVec2(rowPos.x, rowY0),
                                  ImVec2(rowPos.x + labelW + availW, rowY1),
                                  IM_COL32(30,30,35,180));
            }

            if (entry.active) {
                // ── 活动 Pass: 显示名称 + 耗时 + 条形图 ──
                float passMs = entry.timeMs;
                float frac = (msBarMax > 0.0f) ? (std::min)(passMs / msBarMax, 1.0f) : 0.0f;

                // 名称
                dl->AddText(ImVec2(rowPos.x + 4, rowY0 + 5),
                            IM_COL32(220,220,220,255), passName);

                // 时间值
                char timeBuf[32];
                std::snprintf(timeBuf, sizeof(timeBuf), "%.3f ms", passMs);
                float nameW = ImGui::CalcTextSize(passName).x;
                dl->AddText(ImVec2(rowPos.x + 6 + nameW + 8, rowY0 + 5),
                            IM_COL32(150,150,150,200), timeBuf);

                // 条形图
                float barX0 = rowPos.x + labelW;
                float barW = availW * frac;
                if (barW < 1.0f && frac > 0.0f) barW = 1.0f;

                if (barW > 0.0f) {
                    ImU32 color = passColors[i % 8];
                    dl->AddRectFilled(ImVec2(barX0 + 2, rowY0 + 3),
                                      ImVec2(barX0 + barW, rowY1 - 3),
                                      color, 3.0f);

                    float pct = (totalGPUms > 0.0f) ? (passMs / totalGPUms) * 100.0f : 0.0f;
                    char pctBuf[16];
                    std::snprintf(pctBuf, sizeof(pctBuf), "%.1f%%", pct);
                    dl->AddText(ImVec2(barX0 + barW + 6, rowY0 + 5),
                                IM_COL32(180,180,180,255), pctBuf);
                }
            } else {
                // ── 非活动 Pass: 灰色名称 + "--" ──
                dl->AddText(ImVec2(rowPos.x + 4, rowY0 + 5),
                            IM_COL32(120,120,120,200), passName);
                char disabledBuf[24];
                std::snprintf(disabledBuf, sizeof(disabledBuf), "--");
                float nameW = ImGui::CalcTextSize(passName).x;
                dl->AddText(ImVec2(rowPos.x + 6 + nameW + 8, rowY0 + 5),
                            IM_COL32(80,80,80,200), disabledBuf);
            }

            ImGui::Dummy(ImVec2(0, rowH));
        }

        // ── 底部合计 ──
        ImGui::Separator();
        if (totalGPUms > 0.0f) {
            char summary[128];
            std::snprintf(summary, sizeof(summary),
                          "Total: %.3f ms | %.1f FPS budget",
                          totalGPUms, 1000.0f / (std::max)(totalGPUms, 0.001f));
            ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1), "%s", summary);
        }
    }

    // ── 6. View Modes ────────────────────────────────────────────

    void PerformanceWindow::DrawViewModes() {
        if (!m_RenderContext) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "No render context set. Call SetRenderContext()\n"
                               "in Application startup to enable view mode switching.");
            return;
        }

        ViewMode currentMode = m_RenderContext->GetViewMode();

        // ── 当前模式指示器 ──
        {
            ImGui::Text("Current: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0,1,0.8f,1), "%s",
                               ViewModeToString(currentMode));

            // 重置按钮
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
            if (ImGui::Button("Reset##ViewMode", ImVec2(80, 0))) {
                m_RenderContext->SetViewMode(ViewMode::Normal);
            }
        }

        ImGui::Separator();
        ImGui::Spacing();

        // ── 分组: 基础模式 ──
        ImGui::TextColored(ImVec4(0.8f,0.8f,0.3f,1), "Basic Modes");
        ImGui::Separator();

        static const ViewMode basicModes[] = {
            ViewMode::Normal,
            ViewMode::Wireframe,
            ViewMode::Unlit,
            ViewMode::LightingOnly,
            ViewMode::NormalMap
        };
        static const char* basicDesc[] = {
            "Standard shaded rendering",
            "Show wireframe only — inspect topology & tiny triangles",
            "Texture-only (Albedo) — eliminate lighting interference",
            "Grey objects with lighting only — inspect shadows & highlights",
            "Visualize normal map direction — detect flipped green channel"
        };
        static const ImVec4 basicColors[] = {
            ImVec4(0.4f,1.0f,0.4f,1),  // Normal: green
            ImVec4(1.0f,1.0f,0.4f,1),  // Wireframe: yellow
            ImVec4(0.4f,0.6f,1.0f,1),  // Unlit: blue
            ImVec4(1.0f,0.6f,0.4f,1),  // Lighting: orange
            ImVec4(0.6f,0.4f,1.0f,1),  // NormalMap: purple
        };

        for (int i = 0; i < 5; ++i) {
            ImGui::PushID(static_cast<int>(basicModes[i]));

            bool isActive = (currentMode == basicModes[i]);

            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();

            // 单选按钮
            if (ImGui::RadioButton("##mode", isActive)) {
                m_RenderContext->SetViewMode(basicModes[i]);
            }
            ImGui::SameLine();

            // 模式名称
            ImGui::TextColored(basicColors[i], "%s", ViewModeToString(basicModes[i]));

            // 模式名称后的图标提示（仅 Wireframe 额外标注）
            if (basicModes[i] == ViewMode::Wireframe) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1,0.5f,0,1), "  (glPolygonMode)");
            }

            // 描述（缩进）
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "  %s", basicDesc[i]);

            ImGui::EndGroup();
            ImGui::Spacing();

            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 分组: PBR 参数可视化 ──
        ImGui::TextColored(ImVec4(0.3f,0.8f,1.0f,1), "PBR Parameter Visualization");
        ImGui::Separator();

        static const ViewMode pbrModes[] = {
            ViewMode::PBR_BaseColor,
            ViewMode::PBR_Roughness,
            ViewMode::PBR_Metallic,
            ViewMode::PBR_Specular
        };
        static const char* pbrDesc[] = {
            "Display Base Color (Albedo) only",
            "Display Roughness channel — white = rough, black = smooth",
            "Display Metallic channel — white = metal, black = dielectric",
            "Display Specular intensity"
        };
        static const ImVec4 pbrColors[] = {
            ImVec4(1.0f,0.8f,0.4f,1),  // Base Color: warm
            ImVec4(0.6f,0.9f,0.6f,1),  // Roughness: green
            ImVec4(0.8f,0.6f,1.0f,1),  // Metallic: purple
            ImVec4(1.0f,0.6f,0.6f,1),  // Specular: red
        };

        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(100 + i);

            bool isActive = (currentMode == pbrModes[i]);

            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();

            if (ImGui::RadioButton("##pbr", isActive)) {
                m_RenderContext->SetViewMode(pbrModes[i]);
            }
            ImGui::SameLine();

            ImGui::TextColored(pbrColors[i], "%s", ViewModeToString(pbrModes[i]));
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "  %s", pbrDesc[i]);

            ImGui::EndGroup();
            ImGui::Spacing();

            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 分组: 着色器复杂度 ──
        ImGui::TextColored(ImVec4(1.0f,0.4f,0.4f,1), "Overdraw Detection");
        ImGui::Separator();

        {
            bool isComplexity = (currentMode == ViewMode::ShaderComplexity);
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();

            if (ImGui::RadioButton("##complexity", isComplexity)) {
                m_RenderContext->SetViewMode(
                    isComplexity ? ViewMode::Normal : ViewMode::ShaderComplexity);
            }
            ImGui::SameLine();

            ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "%s",
                               ViewModeToString(ViewMode::ShaderComplexity));
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "  Visualize shader instruction cost via color gradient\n"
                               "  Green = cheap, Yellow = moderate, Red = expensive\n"
                               "  Helps identify overdraw and heavy fragment shaders");

            ImGui::EndGroup();
        }

        // ── 当前模式生效提示 ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (currentMode != ViewMode::Normal) {
            ImGui::TextColored(ImVec4(1,0.8f,0.2f,1),
                               "⚠ Debug mode active: %s",
                               ViewModeToString(currentMode));
            ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1),
                               "   u_ViewMode = %d is set as shader uniform.\n"
                               "   Wireframe mode also sets glPolygonMode(GL_LINE).",
                               ViewModeToShaderInt(currentMode));
        }
    }

    // ── 7. Buffer Visualization ──────────────────────────────────

    void PerformanceWindow::DrawBufferVisualization() {
        if (!m_RenderContext) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "No render context set.");
            return;
        }

        // ── 模式选择 ──
        ImGui::Text("View Buffer:");
        ImGui::SameLine();
        {
            const char* items[] = {
                "Depth Buffer",
                "G-Buffer: Normal",
                "G-Buffer: Albedo",
                "G-Buffer: Roughness",
                "G-Buffer: Metallic",
                "Stencil Buffer",
                "Motion Vectors"
            };
            int currentItem = static_cast<int>(m_BufferVisSelectedMode) - 1;
            if (currentItem < 0) currentItem = 0;

            ImGui::PushItemWidth(220);
            if (ImGui::Combo("##BufferSelect", &currentItem, items, IM_ARRAYSIZE(items))) {
                m_BufferVisSelectedMode = static_cast<BufferVisMode>(currentItem + 1);
            }
            ImGui::PopItemWidth();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 根据模式显示不同缓冲区 ──
        switch (m_BufferVisSelectedMode) {
            case BufferVisMode::Depth: {
                ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1),
                                   "Depth Buffer — Linearized (near=white, far=black)");

                // 点击刷新
                if (ImGui::Button("Capture Depth", ImVec2(140, 0))) {
                    m_DepthCacheValid = m_RenderContext->ReadDepthBuffer(
                        m_CachedDepthBuffer, m_CachedDepthW, m_CachedDepthH);
                }
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                   "  Size: %dx%d", m_CachedDepthW, m_CachedDepthH);

                ImGui::Spacing();

                if (m_DepthCacheValid && m_CachedDepthW > 0 && m_CachedDepthH > 0) {
                    // 显示深度图作为纹理
                    // 注：这里利用 ImGui::Image 显示深度缓冲区
                    // 由于 ImGui 需要 GLuint 纹理 ID，我们使用读回的像素数据生成临时纹理
                    // 简化方案：将 float depth 映射为灰度图，创建 ImGui 纹理显示

                    // 获取可用区域，保持宽高比
                    float previewW = ImGui::GetContentRegionAvail().x;
                    float aspect = static_cast<float>(m_CachedDepthW) / m_CachedDepthH;
                    float previewH = previewW / aspect;
                    if (previewH > 300.0f) {
                        previewH = 300.0f;
                        previewW = previewH * aspect;
                    }

                    // 将 float 深度数据转为 RGBA 纹理显示
                    // 使用 ImGui::Image 需要上传到 GL 纹理
                    // 为了简化不引入每帧创建纹理的开销，我们使用像素值绘制灰度预览
                    // 使用 ImDrawList 直接在 UI 上逐像素绘制
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 pos = ImGui::GetCursorScreenPos();

                    // 绘制缩略图框架
                    dl->AddRectFilled(pos, ImVec2(pos.x + previewW, pos.y + previewH),
                                      IM_COL32(20,20,20,255));
                    dl->AddRect(pos, ImVec2(pos.x + previewW, pos.y + previewH),
                                IM_COL32(100,100,100,180));

                    // 使用简化方法: 降采样绘制灰度像素块
                    // 步长控制降采样倍数，避免绘制过多像素
                    int stepX = (std::max)(1, m_CachedDepthW / static_cast<int>(previewW));
                    int stepY = (std::max)(1, m_CachedDepthH / static_cast<int>(previewH));
                    stepX = (std::max)(stepX, stepY);
                    stepY = stepX;

                    float scaleX = previewW / (std::max)(1, m_CachedDepthW / stepX);
                    float scaleY = previewH / (std::max)(1, m_CachedDepthH / stepY);

                    for (int y = 0; y < m_CachedDepthH; y += stepY) {
                        for (int x = 0; x < m_CachedDepthW; x += stepX) {
                            size_t idx = y * m_CachedDepthW + x;
                            if (idx >= m_CachedDepthBuffer.size()) continue;
                            float depth = m_CachedDepthBuffer[idx];
                            // 钳制到 [0,1]
                            depth = (std::max)(0.0f, (std::min)(1.0f, depth));
                            uint8 val = static_cast<uint8>(depth * 255.0f);
                            ImU32 color = IM_COL32(val, val, val, 255);

                            float px = pos.x + (x / stepX) * scaleX;
                            float py = pos.y + (y / stepY) * scaleY;
                            dl->AddRectFilled(ImVec2(px, py),
                                              ImVec2(px + scaleX + 0.5f, py + scaleY + 0.5f),
                                              color);
                        }
                    }

                    // 中间标注
                    char previewLabel[64];
                    std::snprintf(previewLabel, sizeof(previewLabel),
                                  "Depth (%dx%d) - preview", m_CachedDepthW, m_CachedDepthH);
                    ImGui::SetCursorScreenPos(ImVec2(pos.x + 4, pos.y + previewH + 4));
                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "%s", previewLabel);

                    ImGui::Dummy(ImVec2(0, previewH + 20));
                } else {
                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                       "Click 'Capture Depth' to read the depth buffer.\n"
                                       "Note: Requires a depth attachment on the current FBO.");
                }
                break;
            }

            case BufferVisMode::Normal:
            case BufferVisMode::Albedo:
            case BufferVisMode::Roughness:
            case BufferVisMode::Metallic: {
                // G-Buffer 各层 —— 通过纹理 ID 在 ImGui 中直接显示
                BufferTextureHandle texHandle = 0;
                const char* texName = "G-Buffer";
                switch (m_BufferVisSelectedMode) {
                    case BufferVisMode::Normal:    texHandle = m_BufferVisData.gbufferNormal;   texName = "Normal"; break;
                    case BufferVisMode::Albedo:    texHandle = m_BufferVisData.gbufferAlbedo;   texName = "Albedo"; break;
                    case BufferVisMode::Roughness: texHandle = m_BufferVisData.gbufferRoughness; texName = "Roughness"; break;
                    case BufferVisMode::Metallic:  texHandle = m_BufferVisData.gbufferMetallic;  texName = "Metallic"; break;
                    default: break;
                }

                if (texHandle != 0) {
                    ImGui::TextColored(ImVec4(0.4f,1.0f,0.4f,1),
                                       "G-Buffer: %s (tex ID: %llu)", texName, texHandle);

                    float previewW = ImGui::GetContentRegionAvail().x;
                    float previewH = previewW * 0.6f; // 近似 5:3 宽高比
                    if (previewH > 300.0f) previewH = 300.0f;

                    // 使用 ImGui::Image 显示 OpenGL 纹理
                    // 需要将 GLuint 纹理 ID 转为 ImTextureID
                    ImTextureID texID = (ImTextureID)(uintptr_t)(texHandle);
                    ImGui::Image(texID, ImVec2(previewW, previewH));

                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                       "Size: %dx%d (from renderer)",
                                       m_BufferVisData.width, m_BufferVisData.height);
                } else {
                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                       "No G-Buffer texture available.\n"
                                       "Enable deferred rendering or provide buffer handles.");
                }
                break;
            }

            case BufferVisMode::Stencil: {
                BufferTextureHandle stencil = m_BufferVisData.stencilTexture;
                if (stencil != 0) {
                    ImGui::TextColored(ImVec4(0.4f,1.0f,0.4f,1),
                                       "Stencil Buffer (tex ID: %llu)", stencil);

                    float previewW = ImGui::GetContentRegionAvail().x;
                    float previewH = previewW * 0.6f;
                    if (previewH > 300.0f) previewH = 300.0f;

                    ImTextureID texID = (ImTextureID)(uintptr_t)(stencil);
                    ImGui::Image(texID, ImVec2(previewW, previewH));

                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                       "Useful for debugging stencil-based outlines/masks.");
                } else {
                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                       "Stencil buffer not available.");
                }
                break;
            }

            case BufferVisMode::MotionVec: {
                BufferTextureHandle mv = m_BufferVisData.motionVecTexture;
                if (mv != 0) {
                    ImGui::TextColored(ImVec4(0.4f,1.0f,0.4f,1),
                                       "Motion Vectors (tex ID: %llu)", mv);

                    float previewW = ImGui::GetContentRegionAvail().x;
                    float previewH = previewW * 0.6f;
                    if (previewH > 300.0f) previewH = 300.0f;

                    ImTextureID texID = (ImTextureID)(uintptr_t)(mv);
                    ImGui::Image(texID, ImVec2(previewW, previewH));

                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                       "Red/Green channels encode XY motion.\n"
                                       "Debug ghosting issues in TAA/motion blur.");
                } else {
                    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                       "Motion vectors not available.\n"
                                       "Enable velocity buffer in your renderer.");
                }
                break;
            }

            default: break;
        }

        // ── 数据源提示 ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f,0.4f,0.4f,1),
                           "Tip: Your renderer must populate BufferVisFrameData\n"
                           "via IRenderContext::GetBufferVisData() each frame.");
    }

    // ── 8. Lighting & Shadows ─────────────────────────────────────

    void PerformanceWindow::DrawLightingAndShadows() {
        if (!m_RenderContext) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "No render context set.");
            return;
        }

        auto* ld = m_RenderContext->GetLightingDebugData();
        if (!ld) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "Lighting debug data not available.\n"
                               "Your renderer must provide LightingDebugFrameData\n"
                               "via IRenderContext::GetLightingDebugData().");
            return;
        }

        // ── 全局光照开关 ──
        {
            ImGui::TextColored(ImVec4(1,1,0.5f,1), "Global Toggles");
            ImGui::Separator();

            ImGui::Checkbox("Global Lights", &ld->globalLightsEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Ambient Light", &ld->ambientLightEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Shadows", &ld->shadowsEnabled);

            ImGui::Checkbox("SSAO", &ld->ssaoEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("GI (Global Illumination)", &ld->giEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Reflections", &ld->reflectionsEnabled);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 环境光参数 ──
        {
            ImGui::TextColored(ImVec4(0.5f,1.0f,0.5f,1), "Ambient Light");
            ImGui::Separator();

            float col[3] = { ld->ambientColor.x, ld->ambientColor.y, ld->ambientColor.z };
            if (ImGui::ColorEdit3("Ambient Color", col,
                                  ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
                ld->ambientColor = { col[0], col[1], col[2] };
            }
            ImGui::SliderFloat("Ambient Intensity", &ld->ambientIntensity, 0.0f, 5.0f, "%.2f");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 光源列表 ──
        {
            ImGui::TextColored(ImVec4(0.8f,0.8f,1.0f,1), "Light Sources (%u)", ld->lightCount);
            ImGui::Separator();

            for (uint32 i = 0; i < ld->lightCount && i < LightingDebugFrameData::kMaxLights; ++i) {
                auto& light = ld->lights[i];

                ImGui::PushID(i);

                // 折叠面板
                char headerBuf[64];
                std::snprintf(headerBuf, sizeof(headerBuf), "Light %u: %s%s",
                              i, light.name.c_str(),
                              light.enabled ? "" : " (disabled)");

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed |
                                           ImGuiTreeNodeFlags_SpanAvailWidth;
                if (!light.enabled) flags |= ImGuiTreeNodeFlags_DefaultOpen;

                bool headerOpen = ImGui::TreeNodeEx(headerBuf, flags);
                if (!light.enabled && !headerOpen) {
                    // 如果禁用但仍然收起，只显示简单信息
                }

                // 启用/禁用单光源
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
                ImGui::Checkbox("On", &light.enabled);

                if (headerOpen) {
                    float pos[3] = { light.position.x, light.position.y, light.position.z };
                    float col[3] = { light.color.x, light.color.y, light.color.z };

                    ImGui::DragFloat3("Position", pos, 0.1f);
                    ImGui::ColorEdit3("Color", col, ImGuiColorEditFlags_Float);
                    ImGui::SliderFloat("Light Intensity", &light.intensity, 0.0f, 20.0f, "%.1f");
                    ImGui::Checkbox("Cast Shadows", &light.isShadowCaster);

                    light.position = { pos[0], pos[1], pos[2] };
                    light.color    = { col[0], col[1], col[2] };

                    ImGui::TreePop();
                }

                ImGui::PopID();
                ImGui::Spacing();
            }

            if (ld->lightCount == 0) {
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                   "No light sources registered.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 阴影参数 ──
        {
            ImGui::TextColored(ImVec4(1.0f,0.6f,0.3f,1), "Shadow Settings");
            ImGui::Separator();

            ImGui::SliderFloat("Shadow Bias", &ld->shadowBias, 0.0f, 0.05f, "%.5f");
            ImGui::SliderInt("Shadow Map Size", (int*)&ld->shadowMapSize, 256, 4096);
            ImGui::Checkbox("Show Shadow Map", &ld->showShadowMap);

            // Shadow Map 预览
            if (ld->showShadowMap && ld->shadowTextureHandle != 0) {
                float previewW = ImGui::GetContentRegionAvail().x;
                float previewH = previewW;

                ImTextureID texID = (ImTextureID)(uintptr_t)(ld->shadowTextureHandle);
                ImGui::Image(texID, ImVec2(previewW, previewH));

                char smLabel[64];
                std::snprintf(smLabel, sizeof(smLabel),
                              "Shadow Map (%dx%d)", ld->shadowTexWidth, ld->shadowTexHeight);
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "%s", smLabel);
            }

            // ── 阴影级联可视化 ──
            ImGui::Spacing();
            ImGui::Text("Shadow Cascades:");
            ImGui::SameLine();
            ImGui::Checkbox("Visualize Cascades", &ld->cascades.visualizeCascades);

            if (ld->cascades.cascadeCount > 0) {
                // 显示级联分割线
                char splitsStr[128] = "";
                for (uint32 c = 0; c < ld->cascades.cascadeCount; ++c) {
                    char seg[32];
                    std::snprintf(seg, sizeof(seg), "C%d: %.2f  ", c, ld->cascades.cascadeSplits[c]);
                    std::strncat(splitsStr, seg, sizeof(splitsStr) - strlen(splitsStr) - 1);
                }
                ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "%s", splitsStr);

                // 级联颜色显示
                static const ImU32 cascadeColors[] = {
                    IM_COL32(255, 50, 50, 120),
                    IM_COL32(50, 255, 50, 120),
                    IM_COL32(50, 50, 255, 120),
                    IM_COL32(255, 255, 50, 120),
                };
                const int numCascades = (std::min)(static_cast<int>(ld->cascades.cascadeCount), 4);
                for (int c = 0; c < numCascades; ++c) {
                    char label[32];
                    std::snprintf(label, sizeof(label), "Cascade %d", c);
                    ImGui::ColorButton(label, ImVec4(
                        ((cascadeColors[c] >> 0) & 0xFF) / 255.0f,
                        ((cascadeColors[c] >> 8) & 0xFF) / 255.0f,
                        ((cascadeColors[c] >> 16) & 0xFF) / 255.0f, 0.5f),
                        ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
                    ImGui::SameLine();
                    ImGui::Text(" %s  ", label);
                    ImGui::SameLine();
                }
                ImGui::NewLine();

                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                   "Cascade visualization shows each cascade in\n"
                                   "a different color to debug transition seams.");
            } else {
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                   "No cascade data available.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 反射调试 ──
        {
            ImGui::TextColored(ImVec4(0.3f,0.8f,1.0f,1), "Reflection Debug");
            ImGui::Separator();

            ImGui::Checkbox("Show Reflection Probes", &ld->showReflectionProbes);
            ImGui::SameLine();
            ImGui::Checkbox("Show SSR Preview", &ld->showSSR);

            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "Reflection probe visualization and SSR overlay\n"
                               "require reflection capture data from your renderer.");
        }

        // ── 底部提示 ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f,0.4f,0.4f,1),
                           "Tip: To populate lighting data, call\n"
                           "  ctx->GetLightingDebugData() and fill in\n"
                           "  the light list and parameters each frame.");
    }

    // ── 9. Geometry & Culling ────────────────────────────────────

    void PerformanceWindow::DrawGeometryAndCulling() {
        if (!m_RenderContext) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "No render context set.");
            return;
        }

        auto* gd = m_RenderContext->GetGeometryDebugData();
        if (!gd) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "Geometry debug data not available.\n"
                               "Your renderer must provide GeometryDebugFrameData\n"
                               "via IRenderContext::GetGeometryDebugData().");
            return;
        }

        // ── 包围盒调试 ──
        {
            ImGui::TextColored(ImVec4(1,1,0.5f,1), "Bounding Boxes");
            ImGui::Separator();

            ImGui::Checkbox("Show AABB Wireframes", &gd->showAABB);
            ImGui::SameLine();
            ImGui::Checkbox("Show OBB", &gd->showOBB);
            ImGui::SameLine();
            ImGui::Checkbox("Only Culled", &gd->showOnlyCulled);

            // 场景总包围盒信息
            if (gd->sceneBounds.IsValid()) {
                Vec3 size = {
                    gd->sceneBounds.max.x - gd->sceneBounds.min.x,
                    gd->sceneBounds.max.y - gd->sceneBounds.min.y,
                    gd->sceneBounds.max.z - gd->sceneBounds.min.z
                };
                ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1),
                                   "Scene Bounds: (%.1f, %.1f, %.1f) - (%.1f, %.1f, %.1f)  Size: %.1f x %.1f x %.1f",
                                   gd->sceneBounds.min.x, gd->sceneBounds.min.y, gd->sceneBounds.min.z,
                                   gd->sceneBounds.max.x, gd->sceneBounds.max.y, gd->sceneBounds.max.z,
                                   size.x, size.y, size.z);
            } else {
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                   "Scene bounds not available.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 视锥剔除调试 ──
        {
            ImGui::TextColored(ImVec4(0.8f,0.8f,1.0f,1), "Frustum Culling");
            ImGui::Separator();

            ImGui::Checkbox("Enable Frustum Culling", &gd->frustumCullingEnabled);

            // 冻结视锥
            ImGui::SameLine();
            ImGui::Checkbox("Freeze Frustum", &gd->frustumFreeze);
            if (gd->frustumFreeze) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1,0.5f,0,1), "   (FROZEN)");
            }

            ImGui::Spacing();

            // 统计大数字显示
            char totalStr[32], visibleStr[32], culledStr[32];
            std::snprintf(totalStr, sizeof(totalStr), "%u", gd->totalObjects);
            std::snprintf(visibleStr, sizeof(visibleStr), "%u", gd->visibleObjects);
            std::snprintf(culledStr, sizeof(culledStr), "%u", gd->culledObjects);

            if (ImGui::BeginTable("##CullStats", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.5f,0.5f,1.0f,1), "Total");
                ImGui::Text("%s", totalStr);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(0.3f,1.0f,0.3f,1), "Visible");
                ImGui::Text("%s", visibleStr);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(ImVec4(1.0f,0.3f,0.3f,1), "Culled");
                ImGui::Text("%s", culledStr);

                ImGui::EndTable();
            }

            // 剔除比例进度条
            float cullRatio = (gd->totalObjects > 0)
                ? static_cast<float>(gd->culledObjects) / gd->totalObjects
                : 0.0f;
            ImGui::Text("Cull Ratio: %.1f%%", cullRatio * 100.0f);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float barW = ImGui::GetContentRegionAvail().x;
            float barH = 16.0f;
            DrawGradientBar(dl, pos, ImVec2(barW, barH), cullRatio,
                            IM_COL32(60, 200, 60, 255), IM_COL32(60, 60, 200, 255));
            ImGui::Dummy(ImVec2(0, barH + 4));

            ImGui::Spacing();

            // 提示
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "Freeze: locks current frustum, move camera to\n"
                               "check which objects are incorrectly culled.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── LOD 颜色可视化 ──
        {
            ImGui::TextColored(ImVec4(0.3f,1.0f,0.5f,1), "LOD Visualization");
            ImGui::Separator();

            ImGui::Checkbox("Visualize LOD Levels", &gd->lod.visualizeLOD);

            if (gd->lod.currentLODCount > 0) {
                ImGui::Text("Active LOD Levels: %u", gd->lod.currentLODCount);

                // 显示 LOD 颜色表
                static const ImU32 lodColors[] = {
                    IM_COL32(0,   220, 0,   200),  // LOD0: 绿
                    IM_COL32(220, 220, 0,   200),  // LOD1: 黄
                    IM_COL32(220, 140, 0,   200),  // LOD2: 橙
                    IM_COL32(220, 60,  60,  200),  // LOD3+: 红
                };
                static const char* lodLabels[] = {
                    "LOD 0 (High)  ",
                    "LOD 1 (Medium)",
                    "LOD 2 (Low)   ",
                    "LOD 3+ (Ultra) "
                };
                const int visLODs = (std::min)(static_cast<int>(gd->lod.currentLODCount), 4);
                for (int l = 0; l < visLODs; ++l) {
                    ImGui::ColorButton("##lod", ImVec4(
                        ((lodColors[l] >> 0) & 0xFF) / 255.0f,
                        ((lodColors[l] >> 8) & 0xFF) / 255.0f,
                        ((lodColors[l] >> 16) & 0xFF) / 255.0f, 0.8f),
                        ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
                    ImGui::SameLine();
                    ImGui::Text("%s", lodLabels[l]);
                    if (l < visLODs - 1) ImGui::SameLine();
                }
                ImGui::NewLine();

                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                   "LOD visualization colors each mesh by its\n"
                                   "current LOD level to detect abrupt transitions.");
            } else {
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                   "No LOD data available. Your renderer must\n"
                                   "set currentLODCount and assign LOD colors.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 遮挡剔除调试 ──
        {
            ImGui::TextColored(ImVec4(1.0f,0.6f,0.5f,1), "Occlusion Culling");
            ImGui::Separator();

            ImGui::Checkbox("Enable Occlusion Culling", &gd->occlusionCullingEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Show Occlusion Bounds", &gd->showOcclusionBounds);

            if (gd->occludedObjects > 0) {
                char occBuf[32];
                std::snprintf(occBuf, sizeof(occBuf), "%u", gd->occludedObjects);
                ImGui::Text("Occluded this frame: ");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "%s", occBuf);

                float occRatio = (gd->totalObjects > 0)
                    ? static_cast<float>(gd->occludedObjects) / gd->totalObjects
                    : 0.0f;
                ImGui::ProgressBar(occRatio, ImVec2(-1, 0),
                                   "Occlusion ratio");
            } else {
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                   "No occluded objects. Enable occlusion culling\n"
                                   "in your renderer to populate this value.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 场景图统计 ──
        {
            const auto& stats = gd->sceneGraphStats;
            ImGui::TextColored(ImVec4(0.5f,0.8f,1.0f,1), "Scene Graph Stats");
            ImGui::Separator();

            if (stats.totalObjects > 0) {
                if (ImGui::BeginTable("##SGStats", 2, ImGuiTableFlags_SizingStretchSame)) {
                    auto row = [](const char* label, const char* val) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", label);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", val);
                    };

                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%u", stats.totalObjects);
                    row("Total Objects", buf);

                    std::snprintf(buf, sizeof(buf), "%u (%.1f%%)", stats.visibleCount,
                                  (stats.totalObjects > 0) ? (stats.visibleCount * 100.0f / stats.totalObjects) : 0);
                    row("Visible After Cull", buf);

                    std::snprintf(buf, sizeof(buf), "%u", stats.nodeCount);
                    row("Internal Nodes", buf);

                    std::snprintf(buf, sizeof(buf), "%.3f ms", stats.buildTimeMs);
                    row("Build Time", buf);

                    std::snprintf(buf, sizeof(buf), "%.3f ms", stats.queryTimeMs);
                    row("Query Time", buf);

                    std::snprintf(buf, sizeof(buf), "%.2f%%", stats.cullRatio * 100.0f);
                    row("Cull Efficiency", buf);

                    ImGui::EndTable();
                }
            } else {
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                   "Scene graph stats not available.\n"
                                   "Populate via SceneGraphStats from ISceneGraph.");
            }
        }

        // ── 底部集成提示 ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f,0.4f,0.4f,1),
                           "Tip: Fill GeometryDebugFrameData in your renderer:\n"
                           "  gd->totalObjects = scene objects count;\n"
                           "  gd->visibleObjects = after frustum cull;\n"
                           "  gd->sceneGraphStats = sceneGraph->GetStats();");
    }

    // ── 10. Post-Processing ──────────────────────────────────────

    void PerformanceWindow::DrawPostProcessing() {
        if (!m_RenderContext) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "No render context set.");
            return;
        }

        auto* pp = m_RenderContext->GetPostProcessingDebugData();
        if (!pp) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "Post-processing debug data not available.\n"
                               "Your renderer must provide PostProcessingDebugFrameData\n"
                               "via IRenderContext::GetPostProcessingDebugData().");
            return;
        }

        // ── 总开关 ──
        {
            ImGui::TextColored(ImVec4(1,1,0.5f,1), "Master Control");
            ImGui::Separator();

            ImGui::Checkbox("Enable Post-Processing", &pp->postProcessingEnabled);

            // 一键禁用按钮
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
            if (ImGui::Button("Disable All", ImVec2(120, 0))) {
                pp->postProcessingEnabled = false;
                pp->bloomEnabled = false;
                pp->depthOfFieldEnabled = false;
                pp->colorGradingEnabled = false;
                pp->antiAliasingEnabled = false;
                pp->toneMappingEnabled = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset PP", ImVec2(70, 0))) {
                pp->Reset();
            }
        }

        if (!pp->postProcessingEnabled) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1,0.5f,0,1),
                               "⚠ Post-processing is globally disabled.");
            return;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 分项开关 (2x3 网格) ──
        {
            ImGui::TextColored(ImVec4(0.6f,1.0f,0.6f,1), "Individual Effects");
            ImGui::Separator();

            // 第一行: Bloom, DOF, Color Grading
            ImGui::Checkbox("Bloom", &pp->bloomEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Depth of Field", &pp->depthOfFieldEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Color Grading", &pp->colorGradingEnabled);

            // 第二行: AA, ToneMapping, LUT
            ImGui::Checkbox("Anti-Aliasing", &pp->antiAliasingEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Tone Mapping", &pp->toneMappingEnabled);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Bloom 参数 ──
        if (pp->bloomEnabled) {
            ImGui::TextColored(ImVec4(1.0f,0.8f,0.3f,1), "Bloom Settings");
            ImGui::Separator();

            ImGui::SliderFloat("Intensity", &pp->bloomIntensity, 0.0f, 5.0f, "%.2f");
            ImGui::SliderFloat("Threshold", &pp->bloomThreshold, 0.0f, 5.0f, "%.2f");
            ImGui::SliderFloat("Radius", &pp->bloomRadius, 0.0f, 0.5f, "%.3f");

            ImGui::Spacing();
        }

        // ── Depth of Field 参数 ──
        if (pp->depthOfFieldEnabled) {
            ImGui::TextColored(ImVec4(0.3f,0.6f,1.0f,1), "Depth of Field Settings");
            ImGui::Separator();

            ImGui::SliderFloat("Focus Distance", &pp->dofFocusDistance, 0.0f, 100.0f, "%.1f");
            ImGui::SliderFloat("Focus Width", &pp->dofFocusWidth, 0.1f, 20.0f, "%.1f");
            ImGui::SliderFloat("Blur Amount", &pp->dofBlurAmount, 0.0f, 10.0f, "%.1f");

            ImGui::Spacing();
        }

        // ── Color Grading 参数 ──
        if (pp->colorGradingEnabled) {
            ImGui::TextColored(ImVec4(0.8f,0.4f,1.0f,1), "Color Grading / LUT");
            ImGui::Separator();

            ImGui::SliderFloat("Strength", &pp->colorGradingStrength, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Brightness", &pp->brightness, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Contrast", &pp->contrast, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Saturation", &pp->saturation, 0.0f, 2.0f, "%.2f");

            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "LUT texture binding required for full LUT grading.");

            ImGui::Spacing();
        }

        // ── Anti-Aliasing ──
        if (pp->antiAliasingEnabled) {
            ImGui::TextColored(ImVec4(0.3f,1.0f,0.8f,1), "Anti-Aliasing Settings");
            ImGui::Separator();

            ImGui::Checkbox("Enable AA", &pp->aaEnable);
            ImGui::SameLine();

            // AA 类型 Combo
            const char* aaItems[] = { "None", "FXAA", "TAA", "SMAA", "MSAA" };
            int aaIdx = static_cast<int>(pp->aaType);
            if (ImGui::Combo("AA Type", &aaIdx, aaItems, IM_ARRAYSIZE(aaItems))) {
                pp->aaType = static_cast<AAType>(aaIdx);
            }

            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "Current AA mode: %s", AATypeToString(pp->aaType));

            ImGui::Spacing();
        }

        // ── Tone Mapping ──
        if (pp->toneMappingEnabled) {
            ImGui::TextColored(ImVec4(1.0f,0.6f,0.4f,1), "Tone Mapping Settings");
            ImGui::Separator();

            // Tone Mapping 模式 Combo
            const char* tmItems[] = { "None", "Reinhard", "ACES (Filmic)", "Unreal", "Filmic" };
            int tmIdx = static_cast<int>(pp->toneMapMode);
            if (ImGui::Combo("Tone Map Mode", &tmIdx, tmItems, IM_ARRAYSIZE(tmItems))) {
                pp->toneMapMode = static_cast<ToneMappingMode>(tmIdx);
            }

            ImGui::SliderFloat("Exposure", &pp->exposure, 0.1f, 10.0f, "%.2f");
            ImGui::SliderFloat("Gamma", &pp->gamma, 1.0f, 4.0f, "%.2f");

            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "Active: %s | EV: %.1f",
                               ToneMappingModeToString(pp->toneMapMode),
                               std::log2(pp->exposure));
        }

        // ── 底部集成提示 ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f,0.4f,0.4f,1),
                           "Tip: Renderer reads PostProcessingDebugFrameData\n"
                           "each frame to control post-processing pipeline.\n"
                           "Enable/disable effects and tweak parameters live.");
    }

    // ── 11. Textures & Assets ────────────────────────────────────

    void PerformanceWindow::DrawTexturesAndAssets() {
        if (!m_RenderContext) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "No render context set.");
            return;
        }

        auto* td = m_RenderContext->GetTextureDebugData();
        if (!td) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "Texture debug data not available.");
            return;
        }

        // ── 调试开关 ──
        {
            ImGui::TextColored(ImVec4(1,1,0.5f,1), "Texture Debug Tools");
            ImGui::Separator();

            ImGui::Checkbox("Mipmap Color Visualization", &td->showMipmapColors);
            ImGui::SameLine();
            ImGui::Checkbox("Replace with Checkerboard", &td->replaceWithChecker);
            ImGui::SameLine();
            ImGui::Checkbox("Show Streaming Status", &td->showStreamingStatus);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 纹理列表 ──
        {
            ImGui::TextColored(ImVec4(0.5f,1.0f,0.5f,1),
                               "Loaded Textures (%u)", td->textureCount);
            ImGui::Separator();

            if (td->textureCount == 0) {
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                                   "No texture data. Populate TextureDebugFrameData\n"
                                   "in your texture manager each frame.");
            }

            for (uint32 i = 0; i < td->textureCount && i < TextureDebugFrameData::kMaxTextures; ++i) {
                const auto& tex = td->textures[i];
                ImGui::PushID(i);

                char header[64];
                std::snprintf(header, sizeof(header), "%s (%dx%d)",
                              tex.name.c_str(), tex.width, tex.height);
                if (ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_Framed)) {
                    // Mip 级别显示
                    char mipStr[32];
                    std::snprintf(mipStr, sizeof(mipStr), "%u", tex.mipCount);
                    ImGui::Text("Mip Levels: %s", mipStr);

                    // 显存占用
                    if (tex.vramBytes > 0) {
                        char vramBuf[32];
                        FormatBytes(vramBuf, sizeof(vramBuf), tex.vramBytes);
                        ImGui::Text("VRAM: %s", vramBuf);
                    }

                    // Streaming 状态
                    if (td->showStreamingStatus) {
                        if (tex.isStreaming) {
                            ImGui::TextColored(ImVec4(1,1,0,1), "Streaming: %s",
                                               tex.isLoading ? "Loading..." : "Streamed");
                            if (tex.isLoading) {
                                ImGui::ProgressBar(tex.streamProgress, ImVec2(-1,0),
                                                   "Stream progress");
                            }
                        } else {
                            ImGui::TextColored(ImVec4(0,1,0,1), "Resident (memory)");
                        }
                    }

                    // Residency
                    if (!tex.isResident) {
                        ImGui::TextColored(ImVec4(1,0.5f,0,1), "⚠ Not resident");
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
                ImGui::Spacing();
            }
        }

        // ── 总显存 ──
        ImGui::Spacing();
        ImGui::Separator();
        if (td->totalVRAM > 0) {
            char totalVramBuf[32];
            FormatBytes(totalVramBuf, sizeof(totalVramBuf), td->totalVRAM);
            ImGui::Text("Total Texture VRAM: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0,1,0.5f,1), "%s", totalVramBuf);
        }
    }

    // ── 12. Helper Toggles ───────────────────────────────────────

    void PerformanceWindow::DrawHelperToggles() {
        if (!m_RenderContext) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "No render context set.");
            return;
        }

        auto* ht = m_RenderContext->GetHelperTogglesData();
        if (!ht) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "Helper toggle data not available.");
            return;
        }

        // ── 辅助可视化网格 ──
        {
            ImGui::TextColored(ImVec4(1,1,0.5f,1), "Visual Helpers");
            ImGui::Separator();

            ImGui::Checkbox("Show World Grid", &ht->showGrid);
            ImGui::SameLine();
            ImGui::Checkbox("Show Origin Axis", &ht->showOriginAxis);
            ImGui::SameLine();
            ImGui::Checkbox("Show Bones", &ht->showBones);

            ImGui::Checkbox("Show Colliders", &ht->showColliders);

            // 网格参数 (仅网格可见时)
            if (ht->showGrid) {
                ImGui::Indent();
                ImGui::SliderFloat("Grid Size", &ht->gridSize, 1.0f, 100.0f, "%.0f");
                ImGui::SliderInt("Grid Steps", &ht->gridSteps, 1, 50);
                ImGui::Unindent();
            }

            // 碰撞体颜色
            if (ht->showColliders) {
                ImGui::Indent();
                ImGui::ColorEdit4("Collider Color", ht->colliderColor,
                                  ImGuiColorEditFlags_AlphaPreview);
                ImGui::Unindent();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── 渲染控制 ──
        {
            ImGui::TextColored(ImVec4(0.3f,1.0f,0.8f,1), "Render Control");
            ImGui::Separator();

            if (ImGui::Checkbox("Pause Rendering", &ht->pauseRendering)) {
                if (ht->pauseRendering) ht->stepOneFrame = false;
            }

            ImGui::SameLine();

            // 单帧步进按钮
            if (ImGui::Button("Step One Frame", ImVec2(140, 0))) {
                ht->stepOneFrame = true;
                ht->pauseRendering = false;
            }

            if (ht->pauseRendering) {
                ImGui::TextColored(ImVec4(1,0.5f,0,1),
                                   "⏸ Rendering PAUSED. Toggle off or use Step.");
            }

            if (ht->stepOneFrame) {
                ImGui::TextColored(ImVec4(0,1,0.5f,1),
                                   "▶ Stepping one frame...");
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1),
                               "Pause freezes the frame buffer to inspect\n"
                               "transient rendering artifacts. Step advances\n"
                               "by exactly one frame then pauses again.");
        }

        // ── 底部提示 ──
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f,0.4f,0.4f,1),
                           "Tip: Your renderer reads HelperTogglesFrameData\n"
                           "each frame to enable/disable debug overlays.");
    }

} // namespace Engine
