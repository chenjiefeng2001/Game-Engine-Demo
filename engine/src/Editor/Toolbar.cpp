
#include "Engine/Editor/Toolbar.h"
#include "Engine/Editor/IconsFontAwesome6.h"
#include <imgui.h>

namespace Engine {

    // ── 安全的垂直分隔符，严格保持同一行 ──
    static void VerticalSeparator() {
        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // 绘制柔和的竖线
        dl->AddLine(ImVec2(p.x + 2.0f, p.y + 2.0f), ImVec2(p.x + 2.0f, p.y + 22.0f), IM_COL32(80, 80, 90, 160), 1.0f);
        ImGui::Dummy(ImVec2(6.0f, 0.0f));
        ImGui::SameLine(); // 强制光标留在同一行，绝不换行
    }

    // ============================================================
    // 主渲染入口 — P1-c 摘除手术后仅剩两个 WIRED 组：
    //   [播放传送带] | [渲染模式]
    // ============================================================
    void Toolbar::OnImGui() {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

        ImGui::Indent(4.0f);
        // 微调起始 Y 轴，让按钮在 ChildWindow 中垂直居中
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

        // [组1：播放控制]
        ImGui::BeginGroup();
        DrawPlayGroup();
        ImGui::EndGroup();

        VerticalSeparator();

        // [组2：渲染模式]（原藏于 Overlays 弹窗内；弹窗其余项已随死代码摘除）
        ImGui::BeginGroup();
        DrawRenderModeGroup();
        ImGui::EndGroup();

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
                // S1：以真实结果置位 —— PlayAction 返回 false（无场景/克隆
                // 失败等）时保持 Stopped，消灭"假 Playing 态"
                const bool ok = m_PlayCallback && m_PlayCallback();
                if (ok) m_PlayState = PlayState::Playing;
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

            // Pause/Step 为引擎内置 PIE 专属能力；外部播放接管时回调为空，
            // 按钮禁用而非消失（保持布局稳定，杜绝误触）
            ImGui::SameLine();
            auto pauseLabel = (m_PlayState == PlayState::Playing) ? ICON_FA_PAUSE "##pause" : ICON_FA_PLAY "##pause";
            ImGui::BeginDisabled(!m_PauseCallback);
            if (ImGui::Button(pauseLabel, ImVec2(32, 24))) {
                if (m_PauseCallback) m_PauseCallback();
                m_PlayState = (m_PlayState == PlayState::Playing) ? PlayState::Paused : PlayState::Playing;
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(m_PlayState != PlayState::Paused || !m_StepCallback);
            if (ImGui::Button(ICON_FA_FORWARD "##step", ImVec2(32, 24))) {
                if (m_StepCallback) m_StepCallback();
            }
            ImGui::EndDisabled();
        }
    }

    // ============================================================
    // 2. 渲染模式组（平铺 Combo —— 消除弹窗层级）
    // ============================================================
    void Toolbar::DrawRenderModeGroup() {
        ImGui::TextDisabled(ICON_FA_EYE);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        const char* viewModes[] = { "Solid", "Wireframe", "Lighting" };
        if (ImGui::Combo("##ViewMode", &m_ViewMode, viewModes, IM_ARRAYSIZE(viewModes))) {
            if (m_ViewModeCallback) m_ViewModeCallback(m_ViewMode);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Viewport Render Mode");
    }

} // namespace Engine
