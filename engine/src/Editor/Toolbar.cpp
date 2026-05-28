#include "Engine/Editor/Toolbar.h"
#include <imgui.h>

namespace Engine {

    void Toolbar::OnImGui() {
        // 使用 ImGui 的按钮栏风格
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(6.0f, 4.0f));

        // 运行控制
        bool isPlaying = (m_PlayState == PlayState::Playing);
        bool isPaused  = (m_PlayState == PlayState::Paused);

        if (ImGui::Button(isPlaying ? "|> Pause" : "|> Play",
                          ImVec2(80, 0))) {
            if (isPlaying) {
                if (m_PauseCallback) m_PauseCallback();
                m_PlayState = PlayState::Paused;
            } else {
                if (m_PlayCallback) m_PlayCallback();
                m_PlayState = PlayState::Playing;
            }
        }
        ImGui::SameLine();

        if (ImGui::Button("[] Stop", ImVec2(80, 0)) && m_StopCallback) {
            m_StopCallback();
            m_PlayState = PlayState::Stopped;
        }
        ImGui::SameLine();

        // 步进（只在暂停时可用）
        if (isPaused) {
            ImGui::BeginDisabled(false);
        } else {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("-> Step", ImVec2(80, 0)) && m_StepCallback) {
            m_StepCallback();
        }
        if (isPaused) {
            ImGui::EndDisabled();
        } else {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();

        ImGui::Separator();
        ImGui::SameLine();

        // 布局
        if (ImGui::Button("Layout", ImVec2(90, 0)) && m_ResetLayoutCallback) {
            m_ResetLayoutCallback();
        }

        ImGui::PopStyleVar(2);
    }

} // namespace Engine
