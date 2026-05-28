#include "Engine/Editor/MainMenuBar.h"
#include <imgui.h>

namespace Engine {

    void MainMenuBar::OnImGui() {
        if (!ImGui::BeginMainMenuBar()) return;

        DrawFileMenu();
        DrawEditMenu();
        DrawViewMenu();
        DrawHelpMenu();

        // 右侧状态信息
        ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Engine v0.1");

        ImGui::EndMainMenuBar();
    }

    void MainMenuBar::DrawFileMenu() {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene",    "Ctrl+N")) {}
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {}
            if (ImGui::MenuItem("Save Scene",   "Ctrl+S")) {}
            if (ImGui::MenuItem("Save As...",   "Ctrl+Shift+S")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Ctrl+Q")) {
                if (m_ExitCallback) m_ExitCallback();
            }
            ImGui::EndMenu();
        }
    }

    void MainMenuBar::DrawEditMenu() {
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...")) {}
            ImGui::EndMenu();
        }
    }

    void MainMenuBar::DrawViewMenu() {
        if (!m_Visibility) { ImGui::EndMenu(); return; }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene Hierarchy",  "F1", &m_Visibility->sceneHierarchy);
            ImGui::MenuItem("Inspector",         "F2", &m_Visibility->inspector);
            ImGui::MenuItem("Console",           "F3", &m_Visibility->console);
            ImGui::MenuItem("Performance",       "F4", &m_Visibility->performance);
            ImGui::MenuItem("Content Browser",   "F5", &m_Visibility->contentBrowser);
            ImGui::MenuItem("Asset Browser",     "F6", &m_Visibility->assetBrowser);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                // 触发布局重置（通过回调）
            }
            ImGui::EndMenu();
        }
    }

    void MainMenuBar::DrawHelpMenu() {
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                ImGui::OpenPopup("About Engine");
            }
            if (ImGui::MenuItem("ImGui Demo")) {
                // 由 EngineEditor 捕获
            }
            ImGui::EndMenu();
        }

        // About 弹窗
        if (ImGui::BeginPopupModal("About Engine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Game Engine Demo v0.1");
            ImGui::Text("Built with OpenGL 4.6 + Dear ImGui");
            ImGui::Separator();
            ImGui::Text("Libraries: GLFW, GLAD, glm, box2d, OpenAL Soft, stb");
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

} // namespace Engine
