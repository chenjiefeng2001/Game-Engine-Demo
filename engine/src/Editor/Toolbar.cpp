#include "Engine/Editor/Toolbar.h"
#include <imgui.h>

namespace Engine {

    static void Separator() {
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();
    }

    void Toolbar::OnImGui() {
        // ── Play/Stop 控制 ──
        if (m_PlayState == PlayState::Stopped) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
            if (ImGui::Button("> Play", ImVec2(60, 22))) {
                if (m_PlayCallback) m_PlayCallback();
                m_PlayState = PlayState::Playing;
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("| Stop", ImVec2(60, 22))) {
                if (m_StopCallback) m_StopCallback();
                m_PlayState = PlayState::Stopped;
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();
            if (ImGui::Button(m_PlayState == PlayState::Playing ? "||" : ">", ImVec2(28, 22))) {
                if (m_PauseCallback) m_PauseCallback();
                m_PlayState = (m_PlayState == PlayState::Playing)
                    ? PlayState::Paused : PlayState::Playing;
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(m_PlayState != PlayState::Paused);
            if (ImGui::Button(">>", ImVec2(28, 22))) {
                if (m_StepCallback) m_StepCallback();
            }
            ImGui::EndDisabled();
        }

        Separator();

        // ── Gizmo 模式切换 ──
        ImGui::Text("Gizmo:");
        ImGui::SameLine();

        auto GizmoButton = [this](const char* label, int mode, ImVec2 size) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                (m_GizmoMode == mode) ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f)
                                      : ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            if (ImGui::Button(label, size)) { m_GizmoMode = mode; }
            ImGui::PopStyleColor();
        };

        GizmoButton("T##g", 0, ImVec2(28, 22));
        ImGui::SameLine();
        GizmoButton("R##g", 1, ImVec2(28, 22));
        ImGui::SameLine();
        GizmoButton("S##g", 2, ImVec2(28, 22));

        Separator();

        // ── Gizmo 坐标空间 ──
        if (ImGui::Button(m_GizmoLocal ? "Local" : "World", ImVec2(52, 22))) {
            m_GizmoLocal = !m_GizmoLocal;
        }

        Separator();

        // ── 视口模式 ──
        const char* viewModes[] = { "Solid", "Wireframe", "Lighting" };
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("##ViewMode", &m_ViewMode, viewModes, IM_ARRAYSIZE(viewModes));

        Separator();

        // ── 布局重置 ──
        if (ImGui::Button("Reset Layout", ImVec2(90, 22))) {
            if (m_ResetLayoutCallback) m_ResetLayoutCallback();
        }
    }

} // namespace Engine