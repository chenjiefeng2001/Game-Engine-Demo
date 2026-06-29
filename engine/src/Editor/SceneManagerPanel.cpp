#include "Engine/Editor/SceneManagerPanel.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/Level/LevelManager.h"
#include "Engine/Core/Log.h"
#include "Engine/Editor/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h> // 【关键】：引入 string 支持，告别 char[]

namespace Engine {

    SceneManagerPanel::SceneManagerPanel() = default;

    void SceneManagerPanel::OnImGui() {
        if (!m_Visible) return;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (!ImGui::Begin(ICON_FA_MAP_LOCATION_DOT " Scene Manager", &m_Visible, ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        m_CachedProfiles = SceneManager::GetLoadProfiles();

        DrawToolbar();

        if (ImGui::BeginTable("ManagerLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("GroupList", ImGuiTableColumnFlags_WidthFixed, 250.0f);
            ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();

            // ── 左侧：组列表 ──
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
            ImGui::BeginChild("GroupSidebar", ImVec2(0, -28), false);
            DrawGroupSidebar();
            ImGui::EndChild();
            ImGui::PopStyleVar();

            // ── 右侧：组编辑与性能 ──
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginChild("MainArea", ImVec2(0, -28), false);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
            DrawMainArea();
            ImGui::PopStyleVar();
            ImGui::EndChild();

            ImGui::EndTable();
        }

        DrawFooter();
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void SceneManagerPanel::DrawToolbar() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        ImGui::BeginChild("ManagerToolbar", ImVec2(0, 36), false, ImGuiWindowFlags_NoScrollbar);

        if (ImGui::Button(ICON_FA_PLUS " New Group")) {
            m_EditState = SceneGroupEditState{};
            m_EditState.groupName = "NewGroup";
            m_IsEditing = true;
            m_GroupDirty = true;
            m_SelectedGroupIndex = -1;
        }

        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();

        if (ImGui::Button(ICON_FA_GEAR " Sync Build Settings")) {
            // 工业级同步：触发底层资源的重新序列化
            SceneManager::SaveGroupConfig("assets/scenes/scene_groups.json");
            Log::Info("Build settings synced successfully.");
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    void SceneManagerPanel::DrawGroupSidebar() {
        const auto& groups = SceneManager::GetSceneGroups();

        if (groups.empty()) {
            ImGui::TextDisabled("No groups found.");
            if (ImGui::Button("Load Default")) {
                SceneManager::LoadGroupConfig("assets/scenes/scene_groups.json");
            }
            return;
        }

        for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
            const auto& group = groups[i];
            bool isSelected = (i == m_SelectedGroupIndex);

            const char* icon = group.isStreaming ? ICON_FA_WATER : ICON_FA_BOXES_STACK;
            
            ImGui::PushID(i);
            
            // 使用 ImGui::Selectable 实现标准交互
            std::string label = std::string(icon) + " " + group.groupName;
            if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0, 24))) {
                m_SelectedGroupIndex = i;
                m_IsEditing = true;
                
                // 拷贝到编辑状态
                m_EditState.groupName = group.groupName;
                m_EditState.masterScene = group.masterScene;
                m_EditState.description = group.description;
                m_EditState.isStreaming = group.isStreaming;
                m_EditState.entries = group.subScenes;
            }

            // 悬浮时显示加载按钮
            if (ImGui::IsItemHovered()) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 30);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 0.8f));
                if (ImGui::Button(ICON_FA_PLAY, ImVec2(20, 20))) {
                    SceneManager::SwitchSceneAsync(group.groupName, SceneContext{});
                }
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
        }
    }

    void SceneManagerPanel::DrawMainArea() {
        if (!m_IsEditing) {
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() * 0.4f);
            ImGui::TextDisabled("Select a Scene Group to edit properties.");
            return;
        }

        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 1.0f), ICON_FA_PEN_TO_SQUARE " Group Properties");
        ImGui::Separator();
        ImGui::Spacing();

        // ── 属性网格编辑（使用 ImGui::InputText 处理 std::string） ──
        if (ImGui::BeginTable("GroupProps", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            auto DrawStringRow = [](const char* label, std::string& val) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
                // 需要 imgui_stdlib.h 支持
                ImGui::InputText(label, &val);
            };

            DrawStringRow("Group Name", m_EditState.groupName);
            DrawStringRow("Master Scene", m_EditState.masterScene);
            DrawStringRow("Description", m_EditState.description);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("Streaming");
            ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##Streaming", &m_EditState.isStreaming);

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), ICON_FA_LIST " Sub-Scenes");
        if (ImGui::Button(ICON_FA_PLUS " Add Sub-Scene")) {
            m_EditState.entries.push_back({ "NewScene", true, true, 50 });
        }

        // ── 子场景列表 ──
        if (ImGui::BeginTable("SubScenesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Scene Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Required", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Async", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Act", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < m_EditState.entries.size(); ++i) {
                auto& entry = m_EditState.entries[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::InputText("##Name", &entry.sceneName);
                ImGui::TableNextColumn(); ImGui::Checkbox("##Req", &entry.required);
                ImGui::TableNextColumn(); ImGui::Checkbox("##Async", &entry.loadAsync);
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::DragInt("##Pri", &entry.loadPriority, 1, 0, 100);
                
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button(ICON_FA_TRASH)) {
                    m_EditState.entries.erase(m_EditState.entries.begin() + i);
                    i--; // 调整索引
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        
        if (ImGui::Button(ICON_FA_FLOPPY_DISK " Apply Changes", ImVec2(150, 30))) {
            // 将编辑的数据写回底层
            // ... (与你原有的 SaveCurrentGroup 逻辑一致，但不再操作冗余的 char[])
        }
    }

    void SceneManagerPanel::DrawFooter() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::BeginChild("ManagerFooter", ImVec2(0, 24), false);
        ImGui::SetCursorPos(ImVec2(10, 4));
        
        if (SceneManager::IsLoading()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Loading: %s", SceneManager::GetCurrentLoadingScene().c_str());
        } else {
            ImGui::TextDisabled("Status: Ready");
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}
