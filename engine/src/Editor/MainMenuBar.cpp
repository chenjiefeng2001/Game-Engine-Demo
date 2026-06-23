#include "Engine/Editor/MainMenuBar.h"
#include <imgui.h>

namespace Engine {

    void MainMenuBar::OnImGui() {
        if (!ImGui::BeginMainMenuBar()) return;

        DrawFileMenu();
        DrawEditMenu();
        DrawSceneMenu();
        DrawViewMenu();
        DrawToolsMenu();
        DrawHelpMenu();

        // 右侧状态信息
        ImGui::SameLine(ImGui::GetWindowWidth() - 160.0f);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Engine Editor v0.1");

        ImGui::EndMainMenuBar();
    }

    // ============================================================
    // File
    // ============================================================

    void MainMenuBar::DrawFileMenu() {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene",    "Ctrl+N")) {
                if (m_NewSceneCallback) m_NewSceneCallback();
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                if (m_OpenSceneCallback) m_OpenSceneCallback();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene",   "Ctrl+S")) {
                if (m_SaveSceneCallback) m_SaveSceneCallback();
            }
            if (ImGui::MenuItem("Save As...",   "Ctrl+Shift+S")) {
                if (m_SaveAsCallback) m_SaveAsCallback();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Ctrl+Q")) {
                if (m_ExitCallback) m_ExitCallback();
            }
            ImGui::EndMenu();
        }
    }

    // ============================================================
    // Edit
    // ============================================================

    void MainMenuBar::DrawEditMenu() {
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {
                // Undo — 预留
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {
                // Redo — 预留
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...")) {
                // 设置面板
            }
            ImGui::EndMenu();
        }
    }

    // ============================================================
    // Scene
    // ============================================================

    void MainMenuBar::DrawSceneMenu() {
        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("Play",    "F5")) {
                if (m_PlayCallback) m_PlayCallback();
            }
            if (ImGui::MenuItem("Pause",   "F6")) {
                if (m_PauseCallback) m_PauseCallback();
            }
            if (ImGui::MenuItem("Stop",    "Shift+F5")) {
                if (m_StopCallback) m_StopCallback();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Step Frame", "F10")) {
                if (m_StepCallback) m_StepCallback();
            }
            ImGui::EndMenu();
        }
    }

    // ============================================================
    // View
    // ============================================================

    void MainMenuBar::DrawViewMenu() {
        if (!m_Visibility) return;

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene Hierarchy",  nullptr, &m_Visibility->sceneHierarchy);
            ImGui::MenuItem("Inspector",         nullptr, &m_Visibility->inspector);
            ImGui::MenuItem("Console",           nullptr, &m_Visibility->console);
            ImGui::MenuItem("Performance",       nullptr, &m_Visibility->performance);
            ImGui::MenuItem("Content Browser",   nullptr, &m_Visibility->contentBrowser);
            ImGui::MenuItem("Asset Browser",     nullptr, &m_Visibility->assetBrowser);
            ImGui::MenuItem("Dependency Graph",  nullptr, &m_Visibility->depGraph);
            ImGui::MenuItem("Viewport",          nullptr, &m_Visibility->viewport);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                // 重置布局信号（由外部 EngineEditor 处理）
            }
            ImGui::EndMenu();
        }
    }

    // ============================================================
    // Tools
    // ============================================================

    void MainMenuBar::DrawToolsMenu() {
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemo)) {
                if (m_ToggleDemoCallback) m_ToggleDemoCallback();
            }
            ImGui::MenuItem("Metrics", nullptr, nullptr, false);   // 预留
            ImGui::MenuItem("Style Editor", nullptr, nullptr, false); // 预留
            ImGui::EndMenu();
        }
    }

    // ============================================================
    // Help
    // ============================================================

    void MainMenuBar::DrawHelpMenu() {
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                ImGui::OpenPopup("About Engine");
            }
            ImGui::EndMenu();
        }

        // About 弹窗
        if (ImGui::BeginPopupModal("About Engine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Game Engine Demo v0.1");
            ImGui::Separator();
            ImGui::Text("Built with OpenGL 4.6 + Dear ImGui");
            ImGui::Text("Libraries:");
            ImGui::BulletText("GLFW — Window & Input");
            ImGui::BulletText("GLAD — OpenGL Loader");
            ImGui::BulletText("glm — Mathematics");
            ImGui::BulletText("box2d — 2D Physics");
            ImGui::BulletText("OpenAL Soft — 3D Audio");
            ImGui::BulletText("spdlog — Logging");
            ImGui::BulletText("stb — Image Loading");
            ImGui::BulletText("Dear ImGui — Editor UI");
            ImGui::BulletText("ImGuizmo — Transform Gizmo");
            ImGui::BulletText("nlohmann/json — Serialization");
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

} // namespace Engine