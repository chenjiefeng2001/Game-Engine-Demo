#include "Engine/Editor/EngineEditor.h"
#include "Engine/Application.h"
#include "Engine/SceneHierarchyPanel.h"
#include "Engine/InspectorPanel.h"
#include "Engine/ConsolePanel.h"
#include "Engine/PerformanceWindow.h"
#include "Engine/Core/IWindow.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/Log.h"
#include <imgui.h>

namespace Engine {

    EngineEditor::EngineEditor() = default;
    EngineEditor::~EngineEditor() = default;

    void EngineEditor::Init(Application* app) {
        m_App = app;
        if (!app) return;

        // ── PanelVisibility 作为 EngineEditor 的成员（非 static），
        //     与 MainMenuBar 共享同一实例 ──
        m_MenuBar.SetPanelVisibility(&m_Visibility);

        // ── 视口回调 ──
        m_Viewport.SetResizeCallback([app](int32 w, int32 h) {
            if (auto* ctx = app->GetRenderContext()) {
                ctx->OnResize(w, h);
            }
        });

        // ── 菜单栏回调：文件操作与播放控制 ──
        m_MenuBar.SetExitCallback([app]() {
            if (app) {
                auto& window = app->GetWindow();
                window.SetShouldClose(true);
            }
        });

        m_MenuBar.SetNewSceneCallback([]() {
            Log::Info("[Editor] New Scene requested");
        });

        m_MenuBar.SetOpenSceneCallback([]() {
            Log::Info("[Editor] Open Scene requested");
        });

        m_MenuBar.SetSaveSceneCallback([]() {
            Log::Info("[Editor] Save Scene requested");
        });

        m_MenuBar.SetSaveAsCallback([]() {
            Log::Info("[Editor] Save As requested");
        });

        // ── 播放控制回调 ──
        m_MenuBar.SetPlayCallback([this, app]() {
            if (app) {
                app->SetPlaying(true);
                m_Toolbar.SetPlayState(Toolbar::PlayState::Playing);
            }
        });

        m_MenuBar.SetStopCallback([this, app]() {
            if (app) {
                app->SetPlaying(false);
                m_Toolbar.SetPlayState(Toolbar::PlayState::Stopped);
            }
        });

        m_MenuBar.SetPauseCallback([this, app]() {
            if (app && app->IsPlaying()) {
                m_Toolbar.SetPlayState(
                    m_Toolbar.GetPlayState() == Toolbar::PlayState::Playing
                    ? Toolbar::PlayState::Paused : Toolbar::PlayState::Playing);
            }
        });

        m_MenuBar.SetStepCallback([]() {
            Log::Info("[Editor] Step Frame requested");
        });

        // ── 工具栏回调 ──
        m_Toolbar.SetPlayCallback([this, app]() {
            if (app) {
                app->SetPlaying(true);
                m_Toolbar.SetPlayState(Toolbar::PlayState::Playing);
            }
        });

        m_Toolbar.SetStopCallback([this, app]() {
            if (app) {
                app->SetPlaying(false);
                m_Toolbar.SetPlayState(Toolbar::PlayState::Stopped);
            }
        });

        m_Toolbar.SetPauseCallback([this, app]() {
            if (app && app->IsPlaying()) {
                m_Toolbar.SetPlayState(
                    m_Toolbar.GetPlayState() == Toolbar::PlayState::Playing
                    ? Toolbar::PlayState::Paused : Toolbar::PlayState::Playing);
            }
        });

        m_Toolbar.SetResetLayoutCallback([this]() {
            // 重置布局：清除 imgui.ini 中的布局数据
            // 下次重启时会恢复默认 Docking 布局
        });

        // ── 默认可见性 ──
        m_Visibility.sceneHierarchy = true;
        m_Visibility.inspector      = true;
        m_Visibility.console        = true;
        m_Visibility.performance    = true;
        m_Visibility.contentBrowser = false;
        m_Visibility.assetBrowser   = false;
        m_Visibility.depGraph       = false;
        m_Visibility.viewport       = true;
    }

    void EngineEditor::OnUpdate(float32 dt) {
        (void)dt;
    }

    void EngineEditor::OnImGui() {
        // ── 1. 主菜单栏（始终在最顶层） ──
        m_MenuBar.OnImGui();

        // ── 2. 工具栏（浮动窗口，可 dock） ──
        // 不设置固定位置，让 Dockspace 系统管理
        ImGui::Begin("##Toolbar", nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar);
        m_Toolbar.OnImGui();
        ImGui::End();

        // ── 3. 各编辑面板（由 Dockspace 管理布局，用户可自由拖拽） ──

        // 3a. Scene Hierarchy
        if (m_Visibility.sceneHierarchy && m_SceneHierarchy) {
            ImGui::Begin("Scene Hierarchy", &m_Visibility.sceneHierarchy);
            m_SceneHierarchy->OnImGui();
            ImGui::End();
        }

        // 3b. Viewport（中央 3D/2D 视口）
        if (m_Visibility.viewport) {
            m_Viewport.OnImGui();
        }

        // 3c. Inspector
        if (m_Visibility.inspector && m_Inspector) {
            ImGui::Begin("Inspector", &m_Visibility.inspector);
            m_Inspector->OnImGui();
            ImGui::End();
        }

        // 3d. Console
        if (m_Visibility.console && m_Console) {
            ImGui::Begin("Console", &m_Visibility.console);
            m_Console->OnImGui();
            ImGui::End();
        }

        // 3e. Performance
        if (m_Visibility.performance && m_Performance) {
            ImGui::Begin("Performance", &m_Visibility.performance);
            m_Performance->OnImGui();
            ImGui::End();
        }

        // 3f. Content Browser
        if (m_Visibility.contentBrowser) {
            m_ContentBrowser.OnImGui();
        }

        // 3g. Asset Browser
        if (m_Visibility.assetBrowser) {
            m_AssetBrowser.OnImGui();
        }

        // 3h. Dependency Graph
        if (m_Visibility.depGraph) {
            m_DepGraph.OnImGui();
        }

        // ── 4. ImGui Demo（按需） ──
        if (m_ShowDockingDemo) {
            ImGui::ShowDemoWindow(&m_ShowDockingDemo);
        }
    }

    // ============================================================
    // 各面板的包裹窗口（保留但不用于主循环，供外部使用）
    // ============================================================

    void EngineEditor::DrawSceneHierarchyWindow() {
        if (m_SceneHierarchy) {
            ImGui::Begin("Scene Hierarchy");
            m_SceneHierarchy->OnImGui();
            ImGui::End();
        }
    }

    void EngineEditor::DrawInspectorWindow() {
        if (m_Inspector) {
            ImGui::Begin("Inspector");
            m_Inspector->OnImGui();
            ImGui::End();
        }
    }

    void EngineEditor::DrawConsoleWindow() {
        if (m_Console) {
            ImGui::Begin("Console");
            m_Console->OnImGui();
            ImGui::End();
        }
    }

    void EngineEditor::DrawPerformanceWindow() {
        if (m_Performance) {
            ImGui::Begin("Performance");
            m_Performance->OnImGui();
            ImGui::End();
        }
    }

    void EngineEditor::DrawContentBrowserWindow() {
        m_ContentBrowser.OnImGui();
    }

    void EngineEditor::DrawAssetBrowserWindow() {
        m_AssetBrowser.OnImGui();
    }

    void EngineEditor::DrawDepGraphWindow() {
        m_DepGraph.OnImGui();
    }

    void EngineEditor::DrawViewportPanel() {
        m_Viewport.OnImGui();
    }

} // namespace Engine