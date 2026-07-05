#include "Engine/Editor/ScenePanel.h"
#include "Engine/SceneHierarchyPanel.h"
#include "Engine/Editor/SceneViewerPanel.h"
#include "Engine/Editor/SceneManagerPanel.h"
#include <imgui.h>

namespace Engine {

    ScenePanel::ScenePanel() = default;

    void ScenePanel::RegisterPanels(SceneHierarchyPanel* hierarchy,
                                    SceneViewerPanel*    viewer,
                                    SceneManagerPanel*   manager) {
        m_Hierarchy = hierarchy;
        m_Viewer    = viewer;
        m_Manager   = manager;

        // 通过中介者建立面板间通信
        m_Mediator.RegisterHierarchy(hierarchy);
        m_Mediator.RegisterViewer(viewer);
        m_Mediator.RegisterManager(manager);

        // 设置 SceneViewer 的 Solo 回调 → 通知中介者
        if (m_Viewer) {
            // SceneViewerPanel 的 Solo 模式变更通过中介者转发
        }

        // 设置 SceneManager 的加载回调 → 通知中介者
        if (m_Manager) {
            // 加载/卸载场景时通知中介者
        }
    }

    void ScenePanel::SetMode(ScenePanelMode mode) {
        m_Mode = mode;
    }

    void ScenePanel::OnImGui() {
        if (!m_Visible) return;

        if (m_Mode == ScenePanelMode::Tabbed) {
            // ── 合并模式：单个窗口中用 TabBar ──
            ImGui::Begin("Scene Panel", &m_Visible);

            if (ImGui::BeginTabBar("ScenePanelTabs", ImGuiTabBarFlags_None)) {
                // Tab 1: 实体层级树
                if (ImGui::BeginTabItem("Hierarchy")) {
                    DrawHierarchyTab();
                    ImGui::EndTabItem();
                }

                // Tab 2: 场景查看器
                if (ImGui::BeginTabItem("Scene Viewer")) {
                    DrawViewerTab();
                    ImGui::EndTabItem();
                }

                // Tab 3: 场景管理器
                if (ImGui::BeginTabItem("Scene Manager")) {
                    DrawManagerTab();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::End();
        }
        // Split 模式下，ScenePanel 不渲染任何东西
        // 三个面板各自通过 EngineEditor 的其他渲染路径独立显示
    }

    void ScenePanel::DrawHierarchyTab() {
        if (m_Hierarchy) {
            m_Hierarchy->OnImGui();
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                               "Hierarchy panel not registered.");
        }
    }

    void ScenePanel::DrawViewerTab() {
        if (m_Viewer) {
            m_Viewer->OnImGui();
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                               "Viewer panel not registered.");
        }
    }

    void ScenePanel::DrawManagerTab() {
        if (m_Manager) {
            m_Manager->OnImGui();
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                               "Manager panel not registered.");
        }
    }

} // namespace Engine