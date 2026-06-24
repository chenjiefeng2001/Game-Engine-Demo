#include "Engine/Editor/Toolbar.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include <imgui.h>

namespace Engine {

    /// 垂直分隔线 — 用 DrawList 在 BeginChild 本地坐标中绘制
    /// 兼容 ImGui 1.92.x（无 SeparatorEx）
    static void VerticalSeparator() {
        ImGui::SameLine();
        ImVec2 pMin = ImGui::GetCursorScreenPos();
        ImVec2 pMax = ImGui::GetContentRegionMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float y0 = pMin.y;
        float y1 = pMin.y + 24.0f;  // 分隔线高度=按钮高度
        float x  = pMin.x + 2.0f;
        dl->AddLine(ImVec2(x, y0), ImVec2(x, y1),
                    IM_COL32(60, 60, 70, 160), 1.0f);
        ImGui::Dummy(ImVec2(4.0f, 0.0f));
    }

    // ============================================================
    // 设置方法（带回调通知）
    // ============================================================

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
    // 主渲染入口
    // ============================================================

    void Toolbar::OnImGui() {
        // ═══════════════════════════════════════════════════════════
        // 快捷键处理（仅在无其他 UI 项激活时生效）
        // ═══════════════════════════════════════════════════════════
        if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) SetGizmoMode(0);  // Translate
            if (ImGui::IsKeyPressed(ImGuiKey_E)) SetGizmoMode(1);  // Rotate
            if (ImGui::IsKeyPressed(ImGuiKey_R)) SetGizmoMode(2);  // Scale
        }

        // ── 直接在主窗口 ##Toolbar 中布局 ──
        // 没有 BeginChild，所有按钮通过 SameLine 串在同一行。
        // 父窗口已在 EngineEditor 中设置 ImGuiWindowFlags_NoScrollbar 等标志。
        // 按钮间距由 ImGuiStyleVar_ItemSpacing 控制
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

        // 让按钮组垂直居中
        ImGui::SetCursorPosY(4.0f);
        ImGui::Indent(8.0f);

        // ─── 1. 播放控制组 ───
        DrawPlayGroup();
        VerticalSeparator();

        // ─── 2. 变换工具组 (Gizmo) ───
        DrawTransformGroup();
        VerticalSeparator();

        // ─── 3. 吸附与坐标空间 ───
        DrawSnappingGroup();
        VerticalSeparator();

        // ─── 4. 视口设置（Overlays 菜单 + 相机速度） ───
        DrawViewSettingsGroup();

        // ─── 5. 布局重置（右对齐，使用 CursorPos 硬坐标确保不受换行影响） ───
        // 计算从窗口右边缘偏移的位置
        float windowWidth = ImGui::GetWindowContentRegionMax().x + ImGui::GetWindowPos().x - ImGui::GetCursorStartPos().x;
        float rightBtnWidth = 110.0f;
        float cursorX = ImGui::GetCursorPosX();
        float available = windowWidth - cursorX;
        if (available > rightBtnWidth) {
            ImGui::SameLine(ImGui::GetCursorPosX() + available - rightBtnWidth);
        }

        if (ImGui::Button(ICON_FA_REDO " Reset", ImVec2(72, 22))) {
            if (m_ResetLayoutCallback) m_ResetLayoutCallback();
        }

        ImGui::SameLine();
        ImGui::TextDisabled(ICON_FA_QUESTION_CIRCLE);
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

        ImGui::PopStyleVar(2);
    }

    // ============================================================
    // 1. 播放控制组
    // ============================================================

    void Toolbar::DrawPlayGroup() {
        if (m_PlayState == PlayState::Stopped) {
            // 编辑模式：绿色 Play 按钮
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
            if (ImGui::Button(ICON_FA_PLAY " Play", ImVec2(70, 22))) {
                if (m_PlayCallback) m_PlayCallback();
                m_PlayState = PlayState::Playing;
            }
            ImGui::PopStyleColor(2);
        } else {
            // 运行/暂停模式：红色 Stop 按钮
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button(ICON_FA_STOP, ImVec2(32, 22))) {
                if (m_StopCallback) m_StopCallback();
                m_PlayState = PlayState::Stopped;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            const char* pauseLabel = (m_PlayState == PlayState::Playing)
                ? ICON_FA_PAUSE "##pause"
                : ICON_FA_PLAY "##pause";
            if (ImGui::Button(pauseLabel, ImVec2(28, 22))) {
                if (m_PauseCallback) m_PauseCallback();
                m_PlayState = (m_PlayState == PlayState::Playing)
                    ? PlayState::Paused : PlayState::Playing;
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(m_PlayState != PlayState::Paused);
            if (ImGui::Button(ICON_FA_FORWARD "##step", ImVec2(28, 22))) {
                if (m_StepCallback) m_StepCallback();
            }
            ImGui::EndDisabled();
        }
    }

    // ============================================================
    // 2. 变换工具组 (Gizmo)
    // ============================================================

    void Toolbar::DrawTransformGroup() {
        auto GizmoButton = [this](const char* icon, int mode, ImVec2 size) {
            bool isActive = (m_GizmoMode == mode);
            ImGui::PushStyleColor(ImGuiCol_Button,
                isActive ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f)
                         : ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                isActive ? ImVec4(0.4f, 0.6f, 0.9f, 1.0f)
                         : ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
            if (ImGui::Button(icon, size)) { SetGizmoMode(mode); }
            ImGui::PopStyleColor(2);
        };

        // 带图标的 Gizmo 按钮 + 工具提示
        GizmoButton(ICON_FA_ARROWS "##g", 0, ImVec2(28, 22));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate (W)");
        ImGui::SameLine();

        GizmoButton(ICON_FA_ROTATE_RIGHT "##g", 1, ImVec2(28, 22));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate (E)");
        ImGui::SameLine();

        GizmoButton(ICON_FA_EXPAND "##g", 2, ImVec2(28, 22));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale (R)");
        ImGui::SameLine();

        // Local / World 切换
        bool localChanged = false;
        if (ImGui::Button(m_GizmoLocal ? "Local" : "World", ImVec2(52, 22))) {
            m_GizmoLocal = !m_GizmoLocal;
            localChanged = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle between Local and World space");
        }
        if (localChanged && m_GizmoSpaceCallback) {
            m_GizmoSpaceCallback(m_GizmoLocal);
        }
    }

    // ============================================================
    // 3. 吸附与坐标空间
    // ============================================================

    void Toolbar::DrawSnappingGroup() {
        // 吸附磁铁开关按钮
        ImGui::PushStyleColor(ImGuiCol_Text,
            m_SnapEnabled ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f)
                          : ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        bool snapClicked = false;
        if (ImGui::Button(ICON_FA_MAGNET "##snap", ImVec2(28, 22))) {
            m_SnapEnabled = !m_SnapEnabled;
            snapClicked = true;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle Grid Snapping");
        }
        if (snapClicked && m_SnapCallback) {
            m_SnapCallback();
        }

        ImGui::SameLine();

        // 吸附值滑块
        ImGui::SetNextItemWidth(60);
        if (ImGui::DragFloat("##SnapVal", &m_SnapValue, 0.01f, 0.01f, 10.0f, "%.2f")) {
            if (m_SnapValueCallback) m_SnapValueCallback(m_SnapValue);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Snap Value");
        }
    }

    // ============================================================
    // 4. 视口设置组（Overlays 弹出 + 相机速度）
    // ============================================================

    void Toolbar::DrawViewSettingsGroup() {
        // ── Overlays 下拉菜单 ──
        if (ImGui::Button(ICON_FA_EYE " Overlays", ImVec2(75, 22))) {
            ImGui::OpenPopup("OverlaySettings");
        }

        if (ImGui::BeginPopup("OverlaySettings")) {
            // Grid 开关
            bool gridChanged = ImGui::MenuItem(ICON_FA_GRID " Show Grid", nullptr, &m_ShowGrid);
            if (gridChanged && m_GridToggleCallback) m_GridToggleCallback();

            // Gizmos 开关
            bool gizmoChanged = ImGui::MenuItem(ICON_FA_CROSSHAIRS " Show Gizmos", nullptr, &m_ShowGizmos);
            if (gizmoChanged && m_GizmosToggleCallback) m_GizmosToggleCallback();

            // Colliders 开关
            bool colliderChanged = ImGui::MenuItem(ICON_FA_EXPAND " Show Colliders", nullptr, &m_ShowColliders);
            if (colliderChanged && m_CollidersToggleCallback) m_CollidersToggleCallback();

            ImGui::Separator();

            // 视口渲染模式（独立于 Overlays）
            ImGui::TextDisabled("Render Mode");
            const char* viewModes[] = { "Solid", "Wireframe", "Lighting" };
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("##ViewMode", &m_ViewMode, viewModes, IM_ARRAYSIZE(viewModes))) {
                if (m_ViewModeCallback) m_ViewModeCallback(m_ViewMode);
            }

            ImGui::EndPopup();
        }

        // ── 相机速度滑块 ──
        ImGui::SameLine();
        ImGui::TextDisabled(ICON_FA_VIDEO);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::SliderFloat("##CamSpeed", &m_CameraSpeed, 0.1f, 10.0f, "%.1fx", ImGuiSliderFlags_Logarithmic)) {
            if (m_CameraSpeedCallback) m_CameraSpeedCallback(m_CameraSpeed);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Camera Fly Speed");
        }
    }

} // namespace Engine
