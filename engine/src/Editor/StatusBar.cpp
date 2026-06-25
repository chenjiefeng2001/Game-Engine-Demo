#include "Engine/Editor/StatusBar.h"
#include <imgui.h>
#include <algorithm>

namespace Engine {

    void StatusBar::OnImGui() {
        if (!m_Visible) return;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 2));

        // 固定底部状态栏
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - m_Height));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, m_Height));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("##StatusBar", nullptr, flags);

        // ── FPS / 帧耗时（颜色编码） ──
        ImVec4 fpsColor;
        if (m_FrameTimeMs < 16.0f)       fpsColor = ImVec4(0.2f, 0.9f, 0.3f, 1);  // 绿
        else if (m_FrameTimeMs < 33.0f)  fpsColor = ImVec4(0.9f, 0.8f, 0.2f, 1);  // 黄
        else                             fpsColor = ImVec4(0.9f, 0.2f, 0.2f, 1);  // 红

        ImGui::TextColored(fpsColor, "%.0f FPS (%.1f ms)", m_FPS, m_FrameTimeMs);
        ImGui::SameLine();

        // ── Draw Calls / 三角面 ──
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1), "DC:%u Tris:%u", m_DrawCalls, m_Triangles / 1000);
        ImGui::SameLine();

        // ── 内存 ──
        float memPercent = m_TotalMemMB > 0 ? (float)m_UsedMemMB / m_TotalMemMB * 100.0f : 0.0f;
        ImGui::Text("Mem: %llu/%llu MB (%.0f%%)", m_UsedMemMB, m_TotalMemMB, memPercent);
        ImGui::SameLine();

        // ── 分隔线 ──
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // ── Git 分支 ──
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1), "[%s]", m_GitBranch.c_str());
        ImGui::SameLine();

        // ── 目标平台 ──
        ImGui::TextUnformatted(m_TargetPlatform.c_str());
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

        // ── 右对齐：快捷开关 ──
        float rightPos = ImGui::GetWindowWidth() - 8;
        ImGui::SetCursorPosX(rightPos);

        for (auto& toggle : m_QuickToggles) {
            if (!toggle.value) continue;
            ImGui::SameLine();
            bool val = *toggle.value;
            if (val) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
            if (ImGui::Button(toggle.name.c_str(), ImVec2(0, 0))) {
                *toggle.value = !val;
            }
            if (val) ImGui::PopStyleColor();
            if (!toggle.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", toggle.tooltip.c_str());
            }
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
        }

        // ── 版本号（最右侧） ──
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_EditorVersion.c_str());

        ImGui::PopStyleVar(2);
        ImGui::End();
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