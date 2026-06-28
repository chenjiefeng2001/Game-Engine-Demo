#include "Engine/Editor/Toolbar.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include <imgui.h>

namespace Engine {

    /// 垂直分隔线
    static void VerticalSeparator() {
        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float y0 = p.y;
        float y1 = p.y + 24.0f;
        float x  = p.x + 4.0f;
        dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), IM_COL32(60, 60, 70, 160), 1.0f);
        ImGui::Dummy(ImVec2(8.0f, 0.0f));
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
    // 主渲染入口 — 响应式布局，自动适应窗口宽度
    // ============================================================

    void Toolbar::OnImGui() {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

        const float availW = ImGui::GetContentRegionAvail().x;
        const float estimatedFullWidth = 700.0f;  // 所有组 + 分隔线 + Reset 预估总宽
        const bool twoRow = (availW < estimatedFullWidth);

        ImGui::Indent(4.0f);

        if (twoRow) {
            // ═══════════════════════════════════════════
            // 双行布局：窄窗口适配
            // ═══════════════════════════════════════════
            // 第一行：播放控制 + 变换工具 + 吸附设置
            const float rowH = 24.0f;
            float rowY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(rowY + 1.0f);

            DrawPlayGroup();
            VerticalSeparator();
            DrawTransformGroup();
            VerticalSeparator();
            DrawSnappingGroup();

            // 第二行：视口设置 + 右对齐 Reset 按钮
            ImGui::SetCursorPosY(rowY + rowH + 2.0f);
            DrawViewSettingsGroup();

            // 右对齐 Reset 按钮
            float remaining = ImGui::GetContentRegionMax().x - ImGui::GetCursorPosX() - 10.0f;
            if (remaining > 80.0f) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - 80.0f);
                if (ImGui::Button(ICON_FA_REDO, ImVec2(24, 22))) {
                    if (m_ResetLayoutCallback) m_ResetLayoutCallback();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Layout");
            }
        } else {
            // ═══════════════════════════════════════════
            // 单行布局：宽窗口
            // ═══════════════════════════════════════════
            // 使用固定偏移而非 GetContentRegionAvail().y 居中，
            // 避免 AutoResizeY 子窗口中可用高度无穷大导致按钮下沉。
            // 竖向间距由上层 BeginChild 的 WindowPadding 提供。
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

            DrawPlayGroup();
            VerticalSeparator();
            DrawTransformGroup();
            VerticalSeparator();
            DrawSnappingGroup();
            VerticalSeparator();
            DrawViewSettingsGroup();

            float remainingWidth = ImGui::GetContentRegionMax().x - ImGui::GetCursorPosX() - 10.0f;
            if (remainingWidth > 80.0f) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - 80.0f);
                if (ImGui::Button(ICON_FA_REDO, ImVec2(24, 22))) {
                    if (m_ResetLayoutCallback) m_ResetLayoutCallback();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Layout");
            }
        }

        ImGui::PopStyleVar(2);
    }

    // ═══════════════════════════════════════════════════════════════
    // 1. 播放控制组
    // ═══════════════════════════════════════════════════════════════

    void Toolbar::DrawPlayGroup() {
        if (m_PlayState == PlayState::Stopped) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
            if (ImGui::Button(ICON_FA_PLAY " Play", ImVec2(70, 22))) {
                if (m_PlayCallback) m_PlayCallback();
                m_PlayState = PlayState::Playing;
            }
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button(ICON_FA_STOP, ImVec2(32, 22))) {
                if (m_StopCallback) m_StopCallback();
                m_PlayState = PlayState::Stopped;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            auto pauseLabel = (m_PlayState == PlayState::Playing) ? ICON_FA_PAUSE "##pause" : ICON_FA_PLAY "##pause";
            if (ImGui::Button(pauseLabel, ImVec2(28, 22))) {
                if (m_PauseCallback) m_PauseCallback();
                m_PlayState = (m_PlayState == PlayState::Playing) ? PlayState::Paused : PlayState::Playing;
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(m_PlayState != PlayState::Paused);
            if (ImGui::Button(ICON_FA_FORWARD "##step", ImVec2(28, 22))) {
                if (m_StepCallback) m_StepCallback();
            }
            ImGui::EndDisabled();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 2. 变换工具组
    // ═══════════════════════════════════════════════════════════════

    void Toolbar::DrawTransformGroup() {
        auto GizmoButton = [this](const char* icon, int mode, ImVec2 size, const char* tip) {
            bool isActive = (m_GizmoMode == mode);
            ImGui::PushStyleColor(ImGuiCol_Button,
                isActive ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f) : ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                isActive ? ImVec4(0.4f, 0.6f, 0.9f, 1.0f) : ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
            if (ImGui::Button(icon, size)) { SetGizmoMode(mode); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            ImGui::PopStyleColor(2);
        };

        GizmoButton(ICON_FA_ARROWS "##g", 0, ImVec2(28, 22), "Translate (W)");
        ImGui::SameLine();
        GizmoButton(ICON_FA_ROTATE_RIGHT "##g", 1, ImVec2(28, 22), "Rotate (E)");
        ImGui::SameLine();
        GizmoButton(ICON_FA_EXPAND "##g", 2, ImVec2(28, 22), "Scale (R)");
        ImGui::SameLine();

        bool localChanged = false;
        if (ImGui::Button(m_GizmoLocal ? "Local" : "World", ImVec2(52, 22))) {
            m_GizmoLocal = !m_GizmoLocal;
            localChanged = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Local/World space");
        if (localChanged && m_GizmoSpaceCallback) m_GizmoSpaceCallback(m_GizmoLocal);
    }

    // ═══════════════════════════════════════════════════════════════
    // 3. 吸附组
    // ═══════════════════════════════════════════════════════════════

    void Toolbar::DrawSnappingGroup() {
        ImGui::PushStyleColor(ImGuiCol_Text,
            m_SnapEnabled ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        bool snapClicked = false;
        if (ImGui::Button(ICON_FA_MAGNET "##snap", ImVec2(28, 22))) {
            m_SnapEnabled = !m_SnapEnabled;
            snapClicked = true;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Grid Snapping");
        if (snapClicked && m_SnapCallback) m_SnapCallback();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        if (ImGui::DragFloat("##SnapVal", &m_SnapValue, 0.01f, 0.01f, 10.0f, "%.2f")) {
            if (m_SnapValueCallback) m_SnapValueCallback(m_SnapValue);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap Value");
    }

    // ═══════════════════════════════════════════════════════════════
    // 4. 视口设置组
    // ═══════════════════════════════════════════════════════════════

    void Toolbar::DrawViewSettingsGroup() {
        if (ImGui::Button(ICON_FA_EYE " Overlays", ImVec2(75, 22))) {
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

        // 相机速度滑块放在 Overlays 按钮后面，不用 right-justify
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::TextDisabled(ICON_FA_VIDEO);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::SliderFloat("##CamSpeed", &m_CameraSpeed, 0.1f, 10.0f, "%.1fx", ImGuiSliderFlags_Logarithmic)) {
            if (m_CameraSpeedCallback) m_CameraSpeedCallback(m_CameraSpeed);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera Fly Speed");
        ImGui::PopStyleVar();
    }

} // namespace Engine