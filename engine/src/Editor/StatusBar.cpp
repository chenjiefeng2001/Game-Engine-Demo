#include "Engine/Editor/StatusBar.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

namespace Engine {

    void StatusBar::OnImGui() {
        if (!m_Visible) return;

        ImGuiViewport* viewport = ImGui::GetMainViewport();

        // 【核心修复】使用 ImGui 原生的 SideBar API，永远固定在底部
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));

        if (ImGui::BeginViewportSideBar("##MainStatusBar", viewport, ImGuiDir_Down, m_Height,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar))
        {
            if (ImGui::BeginMenuBar()) {
                // ── 1. FPS / 帧耗时（颜色编码） ──
                ImVec4 fpsColor;
                if (m_FrameTimeMs < 16.0f)       fpsColor = ImVec4(0.2f, 0.9f, 0.3f, 1);
                else if (m_FrameTimeMs < 33.0f)  fpsColor = ImVec4(0.9f, 0.8f, 0.2f, 1);
                else                             fpsColor = ImVec4(0.9f, 0.2f, 0.2f, 1);

                // ── 靠左：帧性能 ──
                ImGui::TextColored(fpsColor, "%.0f FPS (%.1f ms)", m_FPS, m_FrameTimeMs);
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); 
                ImGui::SameLine();

                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1), "DC: %u | Tris: %uK", m_DrawCalls, m_Triangles / 1000);
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();

                // ── 内存 ──
                float memPercent = m_TotalMemMB > 0 ? (float)m_UsedMemMB / m_TotalMemMB * 100.0f : 0.0f;
                ImGui::Text("Mem: %llu/%llu MB (%.0f%%)", m_UsedMemMB, m_TotalMemMB, memPercent);
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();

                // ── 后台任务 ──
                {
                    std::lock_guard<std::mutex> lock(m_TaskMutex);
                    for (const auto& task : m_ActiveTasks) {
                        if (task.isIndeterminate) {
                            ImGui::TextUnformatted(task.description.c_str());
                            ImGui::SameLine();
                            ImGui::TextUnformatted("...");
                        } else {
                            ImGui::TextUnformatted(task.description.c_str());
                            ImGui::SameLine();
                            ImGui::ProgressBar(task.progress, ImVec2(60, 0), "");
                        }
                        ImGui::SameLine();
                    }
                }

                // ── 右对齐：系统状态 ──
                // 计算右侧内容所需宽度
                float rightWidth = 40.0f; // 预留版本号
                for (auto& toggle : m_QuickToggles) rightWidth += 50.0f;
                rightWidth += 300.0f; // 平台 + 分支
                float rightOffset = ImGui::GetWindowWidth() - rightWidth;
                ImGui::SameLine(rightOffset);

                // Git 分支（使用可用图标：ICON_FA_CODE）
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), ICON_FA_CODE " %s", m_GitBranch.c_str());
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();

                // 平台版本（使用可用图标：ICON_FA_TERMINAL）
                ImGui::TextDisabled(ICON_FA_TERMINAL " %s | %s", m_TargetPlatform.c_str(), m_EditorVersion.c_str());
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();

                // 快捷开关
                for (auto& toggle : m_QuickToggles) {
                    if (!toggle.value) continue;
                    bool val = *toggle.value;
                    if (val) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
                    if (ImGui::Button(toggle.name.c_str(), ImVec2(0, 0))) {
                        *toggle.value = !val;
                    }
                    if (val) ImGui::PopStyleColor();
                    if (!toggle.tooltip.empty() && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", toggle.tooltip.c_str());
                    ImGui::SameLine();
                }

                ImGui::EndMenuBar();
            }
            ImGui::End();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void StatusBar::PushTask(const BackgroundTask& task) {
        std::lock_guard<std::mutex> lock(m_TaskMutex);
        m_ActiveTasks.push_back(task);
    }

    void StatusBar::UpdateTask(const std::string& name, float progress) {
        std::lock_guard<std::mutex> lock(m_TaskMutex);
        for (auto& task : m_ActiveTasks) {
            if (task.name == name) {
                task.progress = progress;
                break;
            }
        }
    }

    void StatusBar::PopTask(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_TaskMutex);
        m_ActiveTasks.erase(
            std::remove_if(m_ActiveTasks.begin(), m_ActiveTasks.end(),
                [&](const BackgroundTask& t) { return t.name == name; }),
            m_ActiveTasks.end());
    }

    void StatusBar::ShowNotification(const std::string& message, float duration) {
        m_Notifications.push_back({message, 0.0f, duration});
    }

    void StatusBar::UpdateNotifications(float dt) {
        for (auto it = m_Notifications.begin(); it != m_Notifications.end();) {
            it->timer += dt;
            if (it->timer >= it->duration) {
                it = m_Notifications.erase(it);
            } else {
                ++it;
            }
        }
    }

} // namespace Engine