
#include "Engine/Editor/Toolbar.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include <imgui.h>

namespace Engine {

    // ── 修复 1：安全的垂直分隔符，严格保持同一行 ──
    static void VerticalSeparator() {
        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // 绘制柔和的竖线
        dl->AddLine(ImVec2(p.x + 2.0f, p.y + 2.0f), ImVec2(p.x + 2.0f, p.y + 22.0f), IM_COL32(80, 80, 90, 160), 1.0f);
        ImGui::Dummy(ImVec2(6.0f, 0.0f));
        ImGui::SameLine(); // 【核心修复】：强制光标留在同一行，绝不换行
    }

    void Toolbar::SetGizmoMode(int mode) {
        if (m_GizmoMode != mode) {
            m_GizmoMode = mode;
            if (m_GizmoModeCallback) m_GizmoModeCallback(mode);
        }
    }

    void Toolbar::SetGizmoLocal(bool local) {
        if (m_GizmoLocal != local) {
            m_GizmoLocal = local;
            if (m_GizmoSpaceCallback) m_GizmoSpaceCallback(local);
        }
    }

    // ============================================================
    // 主渲染入口 — 稳健的分组打包布局 (Grouped Flex Layout)
    // ============================================================
    void Toolbar::OnImGui() {
        // 快捷键拦截
        if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) SetGizmoMode(0);
            if (ImGui::IsKeyPressed(ImGuiKey_E)) SetGizmoMode(1);
            if (ImGui::IsKeyPressed(ImGuiKey_R)) SetGizmoMode(2);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

        const float availW = ImGui::GetContentRegionAvail().x;
        const float threshold = 720.0f; 
        const bool twoRow = (availW < threshold);

        ImGui::Indent(4.0f);
        // 微调起始 Y 轴，让按钮在 ChildWindow 中垂直居中
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

        // ── 修复 2：使用 BeginGroup 打包每一组功能，杜绝排版污染 ──
        
        // [组1：播放控制]
        ImGui::BeginGroup();
        DrawPlayGroup();
        ImGui::EndGroup();

        VerticalSeparator();

        // [组2：变换工具]
        ImGui::BeginGroup();
        DrawTransformGroup();
        ImGui::EndGroup();

        VerticalSeparator();

        // [组3：吸附设置]
        ImGui::BeginGroup();
        DrawSnappingGroup();
        ImGui::EndGroup();

        // 根据宽度决定是否折行
        if (twoRow) {
            // 空间不足，折跃到下一行
            ImGui::Spacing();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
            
            ImGui::BeginGroup();
            DrawViewSettingsGroup();
            ImGui::EndGroup();
        } else {
            // 空间充足，保持在同一行
            VerticalSeparator();
            
            ImGui::BeginGroup();
            DrawViewSettingsGroup();
            ImGui::EndGroup();
        }

        // ── 右对齐：重置布局按钮 ──
        float resetBtnWidth = 80.0f;
        float rightOffset = ImGui::GetContentRegionMax().x - resetBtnWidth;
        
        // 确保不会覆盖前面的内容
        if (ImGui::GetCursorPosX() < rightOffset) {
            ImGui::SameLine(rightOffset);
            if (ImGui::Button(ICON_FA_REDO " Reset", ImVec2(72, 24))) {
                if (m_ResetLayoutCallback) m_ResetLayoutCallback();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Editor Layout");
        }

        ImGui::PopStyleVar(2);
    }

    // ============================================================
    // 1. 播放控制组
    // ============================================================
    void Toolbar::DrawPlayGroup() {
        if (m_PlayState == PlayState::Stopped) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
            if (ImGui::Button(ICON_FA_PLAY " Play", ImVec2(70, 24))) {
                if (m_PlayCallback) m_PlayCallback();
                m_PlayState = PlayState::Playing;
            }
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button(ICON_FA_STOP, ImVec2(32, 24))) {
                if (m_StopCallback) m_StopCallback();
                m_PlayState = PlayState::Stopped;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            auto pauseLabel = (m_PlayState == PlayState::Playing) ? ICON_FA_PAUSE "##pause" : ICON_FA_PLAY "##pause";
            if (ImGui::Button(pauseLabel, ImVec2(32, 24))) {
                if (m_PauseCallback) m_PauseCallback();
                m_PlayState = (m_PlayState == PlayState::Playing) ? PlayState::Paused : PlayState::Playing;
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(m_PlayState != PlayState::Paused);
            if (ImGui::Button(ICON_FA_FORWARD "##step", ImVec2(32, 24))) {
                if (m_StepCallback) m_StepCallback();
            }
            ImGui::EndDisabled();
        }
    }

    // ============================================================
    // 2. 变换工具组
    // ============================================================
    void Toolbar::DrawTransformGroup() {
        auto GizmoButton = [this](const char* icon, int mode, ImVec2 size, const char* tip) {
            bool isActive = (m_GizmoMode == mode);
            ImGui::PushStyleColor(ImGuiCol_Button, isActive ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f) : ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isActive ? ImVec4(0.4f, 0.6f, 0.9f, 1.0f) : ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
            if (ImGui::Button(icon, size)) { SetGizmoMode(mode); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            ImGui::PopStyleColor(2);
        };

        GizmoButton(ICON_FA_ARROWS "##g", 0, ImVec2(28, 24), "Translate (W)");
        ImGui::SameLine();
        GizmoButton(ICON_FA_ROTATE_RIGHT "##g", 1, ImVec2(28, 24), "Rotate (E)");
        ImGui::SameLine();
        GizmoButton(ICON_FA_EXPAND "##g", 2, ImVec2(28, 24), "Scale (R)");
        ImGui::SameLine();

        ImGui::Dummy(ImVec2(2, 0)); ImGui::SameLine();

        bool localChanged = false;
        if (ImGui::Button(m_GizmoLocal ? "Local" : "World", ImVec2(52, 24))) {
            m_GizmoLocal = !m_GizmoLocal;
            localChanged = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Local/World space");
        if (localChanged && m_GizmoSpaceCallback) m_GizmoSpaceCallback(m_GizmoLocal);
    }

    // ============================================================
    // 3. 吸附组
    // ============================================================
    void Toolbar::DrawSnappingGroup() {
        ImGui::PushStyleColor(ImGuiCol_Text, m_SnapEnabled ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        bool snapClicked = false;
        if (ImGui::Button(ICON_FA_MAGNET "##snap", ImVec2(28, 24))) {
            m_SnapEnabled = !m_SnapEnabled;
            snapClicked = true;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Grid Snapping");
        if (snapClicked && m_SnapCallback) m_SnapCallback();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(50);
        if (ImGui::DragFloat("##SnapVal", &m_SnapValue, 0.01f, 0.01f, 100.0f, "%.1f")) {
            if (m_SnapValueCallback) m_SnapValueCallback(m_SnapValue);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap Value");
    }

    // ============================================================
    // 4. 视口设置组
    // ============================================================
    void Toolbar::DrawViewSettingsGroup() {
        if (ImGui::Button(ICON_FA_EYE " Overlays", ImVec2(75, 24))) {
            ImGui::OpenPopup("OverlaySettings");
        }

        if (ImGui::BeginPopup("OverlaySettings")) {
            ImGui::MenuItem(ICON_FA_GRID " Show Grid", nullptr, &m_ShowGrid);
            ImGui::MenuItem(ICON_FA_CROSSHAIRS " Show Gizmos", nullptr, &m_ShowGizmos);
            ImGui::MenuItem(ICON_FA_EXPAND " Show Colliders", nullptr, &m_ShowColliders);
            ImGui::Separator();
            ImGui::TextDisabled("Render Mode");
            const char* viewModes[] = { "Solid", "Wireframe", "Lighting" };
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("##ViewMode", &m_ViewMode, viewModes, IM_ARRAYSIZE(viewModes))) {
                if (m_ViewModeCallback) m_ViewModeCallback(m_ViewMode);
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::TextDisabled(ICON_FA_VIDEO);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::SliderFloat("##CamSpeed", &m_CameraSpeed, 0.1f, 20.0f, "%.1fx", ImGuiSliderFlags_Logarithmic)) {
            if (m_CameraSpeedCallback) m_CameraSpeedCallback(m_CameraSpeed);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera Fly Speed");
    }

} // namespace Engine
