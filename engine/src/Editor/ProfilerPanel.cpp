#include "Engine/Editor/ProfilerPanel.h"
#include "Engine/Debug/ProfilerCore.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace Engine {

    ProfilerPanel::ProfilerPanel() = default;

    void ProfilerPanel::OnImGui(Debug::ProfilerCore& profiler) {
        if (!m_Visible) return;

        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(320, 200), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::Begin(ICON_FA_COG " Profiler", &m_Visible);

        // Tab 栏：CPU / GPU / Memory / Physics
        if (ImGui::BeginTabBar("##ProfilerModules", ImGuiTabBarFlags_FittingPolicyScroll)) {
            const char* modules[] = { "CPU", "GPU", "Memory", "Physics" };
            for (int i = 0; i < 4; ++i) {
                if (ImGui::TabItemButton(modules[i], ImGuiTabItemFlags_NoReorder)) {
                    m_SelectedModule = i;
                }
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();

        // ── 聚合统计（始终显示） ──
        auto stats = profiler.GetAggregatedStats(60);
        DrawAggregatedStats(stats);

        ImGui::Separator();

        // ── 选定模块详细视图 ──
        switch (m_SelectedModule) {
            case 0: DrawCPUTimeline(profiler); break;
            case 1: // GPU 视图与 CPU 类似
            case 2: DrawMemoryChart(profiler); break;
            case 3: break; // Physics
        }

        // ── 帧捕获控件 ──
        DrawCaptureControls(profiler);

        ImGui::End();
    }

    void ProfilerPanel::DrawAggregatedStats(const Debug::ProfilerCore::AggregatedStats& stats) {
        ImGui::Columns(3, "##ProfilerStats", false);

        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "Frame Time");
        ImGui::Text("Avg: %.1f ms", stats.avgFrameTimeMs);
        ImGui::Text("Min: %.1f ms  Max: %.1f ms", stats.minFrameTimeMs, stats.maxFrameTimeMs);

        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "CPU/GPU");
        ImGui::Text("CPU: %.1f ms", stats.avgCPUms);
        ImGui::Text("GPU: %.1f ms", stats.avgGPUms);

        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.6f, 1.0f), "Draw Calls");
        ImGui::Text("Avg: %.0f", stats.avgDrawCalls);
        ImGui::Text("Total: %u", stats.totalDrawCalls);

        ImGui::Columns(1);
    }

    void ProfilerPanel::DrawCPUTimeline(Debug::ProfilerCore& profiler) {
        ImGui::Text("CPU Timeline (last 120 frames)");
        ImGui::Separator();

        auto frames = profiler.GetRecentFrames(120);
        if (frames.empty()) {
            ImGui::TextDisabled("No data yet...");
            return;
        }

        // 时间轴绘制（简化条形图）
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        float canvasWidth = ImGui::GetContentRegionAvail().x;
        float canvasHeight = 120.0f;
        ImVec2 canvasSize(canvasWidth, canvasHeight);

        ImGui::InvisibleButton("##timeline", canvasSize);
        ImRect canvasRect(canvasPos, ImVec2(canvasPos.x + canvasWidth, canvasPos.y + canvasHeight));
        dl->AddRectFilled(canvasRect.Min, canvasRect.Max, IM_COL32(20, 20, 25, 255));
        dl->AddRect(canvasRect.Min, canvasRect.Max, IM_COL32(60, 60, 70, 255));

        // 绘制阈值线
        float thresholdY13 = canvasPos.y + canvasHeight * (1.0f - 13.0f / 33.0f);
        float thresholdY16 = canvasPos.y + canvasHeight * (1.0f - 16.0f / 33.0f);
        dl->AddLine(ImVec2(canvasPos.x, thresholdY16), ImVec2(canvasPos.x + canvasWidth, thresholdY16),
                    IM_COL32(0, 200, 0, 100));
        dl->AddLine(ImVec2(canvasPos.x, thresholdY13), ImVec2(canvasPos.x + canvasWidth, thresholdY13),
                    IM_COL32(200, 200, 0, 100));

        // 绘制帧时间柱状
        float barWidth = canvasWidth / std::max((float)frames.size(), 1.0f);
        float maxTime = 33.0f;
        for (size_t i = 0; i < frames.size(); ++i) {
            float ft = std::min((float)frames[i]->frameTimeMs, maxTime);
            float h = (ft / maxTime) * canvasHeight;
            float x = canvasPos.x + i * barWidth;
            float y = canvasPos.y + canvasHeight - h;

            ImU32 color;
            if (ft < 16.0f) color = IM_COL32(50, 200, 80, 200);
            else if (ft < 33.0f) color = IM_COL32(200, 180, 50, 200);
            else color = IM_COL32(200, 50, 50, 200);

            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barWidth, canvasPos.y + canvasHeight), color);

            // CPU/GPU 分割
            float cpuH = (std::min((float)frames[i]->cpuTotalMs, maxTime) / maxTime) * canvasHeight;
            dl->AddRectFilled(ImVec2(x, canvasPos.y + canvasHeight - cpuH),
                             ImVec2(x + barWidth * 0.5f, canvasPos.y + canvasHeight),
                             IM_COL32(100, 150, 255, 180));
        }

        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasHeight + 8));

        // 图例
        ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1), "CPU");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.3f, 1), "Frame");
        ImGui::SameLine();
        ImGui::TextDisabled("(Hover to inspect)");
    }

    void ProfilerPanel::DrawMemoryChart(Debug::ProfilerCore& profiler) {
        const auto* frame = profiler.GetLastFrame();
        if (!frame) {
            ImGui::TextDisabled("No memory data...");
            return;
        }

        const auto& mem = frame->memorySample;

        // 内存分布圆环图（简化水平堆叠条）
        ImGui::Text("Memory Distribution");
        float totalBytes = static_cast<float>(mem.textureVRAMBytes + mem.bufferVRAMBytes + mem.usedHeapBytes);
        if (totalBytes > 0) {
            float texFrac = mem.textureVRAMBytes / totalBytes;
            float bufFrac = mem.bufferVRAMBytes / totalBytes;
            float heapFrac = mem.usedHeapBytes / totalBytes;

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            float h = 24.0f;

            dl->AddRectFilled(pos, ImVec2(pos.x + w * texFrac, pos.y + h), IM_COL32(50, 150, 200, 200));
            dl->AddRectFilled(ImVec2(pos.x + w * texFrac, pos.y),
                             ImVec2(pos.x + w * (texFrac + bufFrac), pos.y + h),
                             IM_COL32(200, 100, 50, 200));
            dl->AddRectFilled(ImVec2(pos.x + w * (texFrac + bufFrac), pos.y),
                             ImVec2(pos.x + w, pos.y + h),
                             IM_COL32(100, 200, 100, 200));
            dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(128, 128, 128, 128));

            ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h + 4));
            ImGui::Text("Textures: %.1f MB", mem.textureVRAMBytes / (1024.0 * 1024.0));
            ImGui::SameLine();
            ImGui::Text("Buffers: %.1f MB", mem.bufferVRAMBytes / (1024.0 * 1024.0));
            ImGui::SameLine();
            ImGui::Text("Heap: %.1f MB", mem.usedHeapBytes / (1024.0 * 1024.0));
        }

        // 资源计数
        ImGui::Separator();
        ImGui::Text("Texture Count: %u | Mesh Count: %u | Audio Clips: %u",
                     mem.textureCount, mem.meshCount, mem.audioClipCount);

        // 快照对比
        if (mem.diff.deltaBytes != 0) {
            ImGui::TextColored(mem.diff.deltaBytes > 0 ? ImVec4(1, 0.5f, 0.5f, 1) : ImVec4(0.5f, 1, 0.5f, 1),
                               "Delta: %+.1f KB (%u allocs / %u deallocs)",
                               mem.diff.deltaBytes / 1024.0,
                               mem.diff.allocationCount, mem.diff.deallocationCount);
        }
    }

    void ProfilerPanel::DrawCaptureControls(Debug::ProfilerCore& profiler) {
        ImGui::Separator();
        ImGui::Text("Frame Capture");

        if (profiler.IsCapturing()) {
            if (ImGui::Button("Stop Capture", ImVec2(120, 0))) {
                profiler.StopCapture();
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "RECORDING...");
        } else {
            if (ImGui::Button("Start Capture", ImVec2(120, 0))) {
                profiler.StartCapture();
            }
            ImGui::SameLine();
            if (ImGui::Button("Export Stats", ImVec2(100, 0))) {
                // TODO: export to JSON
            }
        }

        // 显示捕获结果
        const auto& capture = profiler.GetCapture();
        if (!capture.isCapturing && !capture.frames.empty()) {
            ImGui::Text("Captured %zu frames", capture.frames.size());
            if (ImGui::Button("Clear Capture")) {
                const_cast<Debug::FrameCapture&>(capture).frames.clear();
            }
        }
    }

} // namespace Engine