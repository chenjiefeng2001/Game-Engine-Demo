#include "Engine/Editor/Toolbar.h"
#include <imgui.h>

namespace Engine {

    static void Separator() {
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();
    }

    void Toolbar::OnImGui() {
        // ═══════════════════════════════════════════════════════════
        // 快捷键处理（仅在无其他 UI 项激活时生效）
        // ═══════════════════════════════════════════════════════════
        if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) m_GizmoMode = 0;  // Translate
            if (ImGui::IsKeyPressed(ImGuiKey_E)) m_GizmoMode = 1;  // Rotate
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_GizmoMode = 2;  // Scale
        }

        // ═══════════════════════════════════════════════════════════
        // Play/Stop 控制
        // ═══════════════════════════════════════════════════════════
        if (m_PlayState == PlayState::Stopped) {
            // 编辑模式：绿色 Play 按钮
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
            if (ImGui::Button("> Play", ImVec2(60, 22))) {
                if (m_PlayCallback) m_PlayCallback();
                m_PlayState = PlayState::Playing;
            }
            ImGui::PopStyleColor();
        } else {
            // 运行/暂停模式：红色 Stop 按钮
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

        // ═══════════════════════════════════════════════════════════
        // Gizmo 模式切换（带高亮选中状态）
        // ═══════════════════════════════════════════════════════════
        ImGui::Text("Gizmo:");
        ImGui::SameLine();

        auto GizmoButton = [this](const char* label, int mode, ImVec2 size) {
            bool isActive = (m_GizmoMode == mode);
            // 激活态使用蓝色，非激活态使用暗色
            ImGui::PushStyleColor(ImGuiCol_Button,
                isActive ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f)
                         : ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            // 激活态悬停效果更亮
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                isActive ? ImVec4(0.4f, 0.6f, 0.9f, 1.0f)
                         : ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
            if (ImGui::Button(label, size)) { m_GizmoMode = mode; }
            ImGui::PopStyleColor(2);
        };

        GizmoButton("T##g", 0, ImVec2(28, 22));
        ImGui::SameLine();
        GizmoButton("R##g", 1, ImVec2(28, 22));
        ImGui::SameLine();
        GizmoButton("S##g", 2, ImVec2(28, 22));

        Separator();

        // ═══════════════════════════════════════════════════════════
        // Gizmo 坐标空间（Local / World 切换）
        // ═══════════════════════════════════════════════════════════
        if (ImGui::Button(m_GizmoLocal ? "Local" : "World", ImVec2(52, 22))) {
            m_GizmoLocal = !m_GizmoLocal;
        }

        // 提示快捷键
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle between Local and World space (shortcut not set)");
        }

        Separator();

        // ═══════════════════════════════════════════════════════════
        // 视口模式（Solid / Wireframe / Lighting）
        // ═══════════════════════════════════════════════════════════
        const char* viewModes[] = { "Solid", "Wireframe", "Lighting" };
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("##ViewMode", &m_ViewMode, viewModes, IM_ARRAYSIZE(viewModes))) {
            if (m_ViewModeCallback) m_ViewModeCallback(m_ViewMode);
        }

        Separator();

        // ═══════════════════════════════════════════════════════════
        // 布局重置
        // ═══════════════════════════════════════════════════════════
        if (ImGui::Button("Reset Layout", ImVec2(90, 22))) {
            if (m_ResetLayoutCallback) m_ResetLayoutCallback();
        }

        // ═══════════════════════════════════════════════════════════
        // Gizmo 快捷键提示（工具提示）
        // ═══════════════════════════════════════════════════════════
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Gizmo Shortcuts:\n"
                "  W  - Translate\n"
                "  E  - Rotate\n"
                "  R  - Scale\n"
                "\n"
                "Playback:\n"
                "  F5 - Play\n"
                "  F6 - Pause\n"
                "  F7 - Stop"
            );
        }
    }

} // namespace Engine
