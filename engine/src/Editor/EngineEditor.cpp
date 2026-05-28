#include "Engine/Editor/EngineEditor.h"
#include "Engine/Application.h"
#include "Engine/SceneHierarchyPanel.h"
#include "Engine/InspectorPanel.h"
#include "Engine/ConsolePanel.h"
#include "Engine/PerformanceWindow.h"
#include "Engine/Core/IWindow.h"
#include "Engine/Core/IRenderContext.h"
#include <imgui.h>

namespace Engine {

    EngineEditor::EngineEditor() = default;
    EngineEditor::~EngineEditor() = default;

    void EngineEditor::Init(Application* app) {
        m_App = app;
        if (!app) return;

        // ── 连接面板可见性到菜单栏 ──
        static PanelVisibility vis;
        m_MenuBar.SetPanelVisibility(&vis);

        // ── 视口回调 ──
        m_Viewport.SetResizeCallback([app](int32 w, int32 h) {
            if (auto* ctx = app->GetRenderContext()) {
                ctx->OnResize(w, h);
            }
        });

        // ── 菜单栏回调 ──
        m_MenuBar.SetExitCallback([app]() {});

        // ── 工具栏回调 ──
        m_Toolbar.SetResetLayoutCallback([this]() {});

        // ── 默认可见性 ──
        vis.sceneHierarchy = true;
        vis.inspector      = true;
        vis.console        = false;
        vis.performance    = true;
        vis.contentBrowser = false;
        vis.assetBrowser   = false;
        vis.viewport       = true;
    }

    void EngineEditor::OnUpdate(float32 dt) {
        (void)dt;
    }

    void EngineEditor::OnImGui() {
        // ── 1. 主菜单栏 ──
        m_MenuBar.OnImGui();

        // ── 2. 获取可见性状态 ──
        PanelVisibility vis;
        if (auto* pvis = const_cast<PanelVisibility*>(
                []() -> PanelVisibility* {
                    static PanelVisibility v;
                    return &v;
                }())) {
            vis = *pvis;
        }

        // ── 3. 工具栏（浮动在菜单栏下方） ──
        m_DockingInitialized = true;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float menuBarHeight = ImGui::GetFrameHeight();
        float toolbarHeight = 36.0f;
        float toolY = viewport->Pos.y + menuBarHeight;

        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, toolY));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, toolbarHeight));
        ImGui::SetNextWindowBgAlpha(0.9f);

        ImGuiWindowFlags toolFlags = ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoCollapse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("Toolbar", nullptr, toolFlags);
        m_Toolbar.OnImGui();
        ImGui::End();
        ImGui::PopStyleVar();

        // ── 4. 面板工作区 ──
        float workY    = toolY + toolbarHeight;
        float workW    = viewport->Size.x;
        float workH    = (viewport->Pos.y + viewport->Size.y) - workY;

        float sideW   = 280.0f;   // 侧面板宽度
        float bottomH = 200.0f;   // 底部面板高度

        // ── 4a. Scene Hierarchy（左上） ──
        if (vis.sceneHierarchy && m_SceneHierarchy) {
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, workY));
            ImGui::SetNextWindowSize(ImVec2(sideW, workH - bottomH));
            ImGui::Begin("Scene Hierarchy", &vis.sceneHierarchy);
            m_SceneHierarchy->OnImGui();
            ImGui::End();
        }

        // ── 4b. Viewport（中央） ──
        if (vis.viewport) {
            float vpX = viewport->Pos.x + sideW;
            float vpW = workW - sideW * 2;
            ImGui::SetNextWindowPos(ImVec2(vpX, workY));
            ImGui::SetNextWindowSize(ImVec2(vpW, workH - bottomH));
            m_Viewport.OnImGui();
        }

        // ── 4c. Inspector（右上） ──
        if (vis.inspector && m_Inspector) {
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + workW - sideW, workY));
            ImGui::SetNextWindowSize(ImVec2(sideW, workH - bottomH));
            ImGui::Begin("Inspector", &vis.inspector);
            m_Inspector->OnImGui();
            ImGui::End();
        }

        // ── 4d. 底部面板（Console / Performance / Content / Asset） ──
        float bottomY = workY + workH - bottomH;
        float bottomItemW = workW / 4.0f;

        if (vis.console && m_Console) {
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, bottomY));
            ImGui::SetNextWindowSize(ImVec2(bottomItemW, bottomH));
            ImGui::Begin("Console", &vis.console);
            m_Console->OnImGui();
            ImGui::End();
        }

        if (vis.performance && m_Performance) {
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + bottomItemW, bottomY));
            ImGui::SetNextWindowSize(ImVec2(bottomItemW, bottomH));
            ImGui::Begin("Performance", &vis.performance);
            m_Performance->OnImGui();
            ImGui::End();
        }

        if (vis.contentBrowser) {
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + bottomItemW * 2, bottomY));
            ImGui::SetNextWindowSize(ImVec2(bottomItemW, bottomH));
            m_ContentBrowser.OnImGui();
        }

        if (vis.assetBrowser) {
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + bottomItemW * 3, bottomY));
            ImGui::SetNextWindowSize(ImVec2(bottomItemW, bottomH));
            m_AssetBrowser.OnImGui();
        }

        // ── 5. ImGui Demo（按需） ──
        if (m_ShowDockingDemo) {
            ImGui::ShowDemoWindow(&m_ShowDockingDemo);
        }

        // ── 6. 同步可见性到各面板实例 ──
        if (m_SceneHierarchy) m_SceneHierarchy->SetVisible(vis.sceneHierarchy);
        if (m_Console)        m_Console->SetVisible(vis.console);
        if (m_Performance)    m_Performance->SetVisible(vis.performance);
    }

    // ============================================================
    // 各面板的包裹窗口
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

    void EngineEditor::DrawViewportPanel() {
        m_Viewport.OnImGui();
    }

} // namespace Engine
