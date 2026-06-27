    #include "Engine/Editor/EngineEditor.h"
    #include "Engine/Editor/SceneManagerPanel.h"
    #include "Engine/Editor/SceneViewerPanel.h"
    #include "Engine/Editor/ViewModePanel.h"
    #include "Engine/Editor/EditorTools.h"
    #include "Engine/Editor/EditorDefs.h"
    #include "Engine/Editor/ShaderGraph/ShaderNodes.h"

// glm 实验性扩展声明（必须在包含 glm 相关头文件之前）
#define GLM_ENABLE_EXPERIMENTAL

#include "Engine/Application.h"
#include "Engine/SceneHierarchyPanel.h"
#include "Engine/InspectorPanel.h"
#include "Engine/ConsolePanel.h"
#include "Engine/PerformanceWindow.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/IWindow.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Log.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtx/matrix_decompose.hpp>

#include "Engine/Platform/FileDialog.h"

namespace Engine {

    EngineEditor::EngineEditor() = default;
    EngineEditor::~EngineEditor() = default;

    void EngineEditor::Init(Application* app) {
        m_App = app;
        if (!app) return;

        m_MenuBar.SetPanelVisibility(&m_Visibility);

        m_Viewport.SetResizeCallback([app](int32 w, int32 h) {
            if (auto* ctx = app->GetRenderContext()) {
                ctx->OnResize(w, h);
            }
        });

        m_MenuBar.SetExitCallback([app]() {
            if (app) { app->GetWindow().SetShouldClose(true); }
        });

        m_MenuBar.SetNewSceneCallback([this]() {
            Log::Info("[Editor] New Scene");
            Scene* s = m_SceneManager.GetScene();
            if (s) { s->Clear(); s->SetName("Untitled Scene"); }
            if (m_SceneHierarchy) m_SceneHierarchy->SetSelected(nullptr);
            m_SelectedObject.reset();
        });

        m_MenuBar.SetOpenSceneCallback([this](const std::string& path) {
            std::string fp = path;
            if (fp.empty()) {
                fp = FileDialog::OpenFile(
                    "Scene (*.scene)\0*.scene\0All\0*.*\0\0");
            }
            if (!fp.empty()) {
                Scene* s = m_SceneManager.GetScene();
                if (s && s->LoadFromFile(fp)) {
                    if (m_SceneHierarchy) m_SceneHierarchy->SetSelected(nullptr);
                    m_SelectedObject.reset();
                }
            }
        });

        m_MenuBar.SetSaveSceneCallback([this]() {
            Scene* s = m_SceneManager.GetScene();
            if (!s) return;
            std::string fp = FileDialog::SaveFile(
                "Scene (*.scene)\0*.scene\0All\0*.*\0\0", "scene");
            if (!fp.empty()) s->SaveToFile(fp);
        });

        m_MenuBar.SetSaveAsCallback([this]() {
            Scene* s = m_SceneManager.GetScene();
            if (!s) return;
            std::string fp = FileDialog::SaveFile(
                "Scene (*.scene)\0*.scene\0All\0*.*\0\0", "scene");
            if (!fp.empty()) s->SaveToFile(fp);
        });

        auto playFn  = [this]() { m_SceneManager.Play(); };
        auto stopFn  = [this]() { m_SceneManager.Stop(); };
        auto pauseFn = [this]() { m_SceneManager.TogglePause(); };
        auto stepFn  = [this]() { m_SceneManager.StepFrame(); };

        m_MenuBar.SetPlayCallback(playFn);
        m_MenuBar.SetStopCallback(stopFn);
        m_MenuBar.SetPauseCallback(pauseFn);
        m_MenuBar.SetStepCallback(stepFn);

        m_Toolbar.SetPlayCallback(playFn);
        m_Toolbar.SetStopCallback(stopFn);
        m_Toolbar.SetPauseCallback(pauseFn);
        m_Toolbar.SetStepCallback(stepFn);

        m_SceneManager.SetStateChangeCallback([this](EditorState st) {
            switch (st) {
                case EditorState::Edit:  m_Toolbar.SetPlayState(Toolbar::PlayState::Stopped); break;
                case EditorState::Play:  m_Toolbar.SetPlayState(Toolbar::PlayState::Playing); break;
                case EditorState::Pause: m_Toolbar.SetPlayState(Toolbar::PlayState::Paused);  break;
            }
        });

        m_Toolbar.SetViewModeCallback([this](int m) {
            if (auto* ctx = m_App ? m_App->GetRenderContext() : nullptr) {
                switch (m) {
                    case 0: ctx->SetViewMode(ViewMode::Normal);       break;
                    case 1: ctx->SetViewMode(ViewMode::Wireframe);    break;
                    case 2: ctx->SetViewMode(ViewMode::LightingOnly); break;
                    default: ctx->SetViewMode(ViewMode::Normal);      break;
                }
            }
        });

        m_Toolbar.SetResetLayoutCallback([this]() { m_ResetLayoutRequested = true; });

        m_Visibility.sceneHierarchy  = true;
        m_Visibility.inspector       = true;
        m_Visibility.console         = true;
        m_Visibility.performance     = true;
        m_Visibility.contentBrowser  = false;
        m_Visibility.assetBrowser    = false;
        m_Visibility.depGraph        = false;
        m_Visibility.viewport        = true;
        m_Visibility.rendererDebug   = false;
        m_Visibility.sceneManager    = true;   // 场景管理器
        m_Visibility.sceneViewerPanel = true;  // 场景查看器
        m_Visibility.scenePanelTabbed = false; // 默认不合并
        // 其他面板通过 View → Editor Settings 控制

        // ── 初始化 VFX 编辑器 — 使用 CreateDefaultTemplate ──
        {
            m_VFXGraphPanel.SetGraph(&m_VFXGraph);
            m_VFXGraphPanel.SetTitle("VFX Graph Editor");
            m_VFXGraph.CreateDefaultTemplate();
        }

        // ── 初始化 Shader Graph 编辑器 — 创建默认示例图 ──
        {
            m_ShaderGraphPanel.SetGraph(&m_ShaderGraph);
            m_ShaderGraphPanel.SetTitle("Shader Graph Editor");

            // 创建默认的 PBR Master + Color 节点
            auto masterNode = std::make_unique<ShaderGraph::PBRMasterNode>(0);
            uint32 masterId = m_ShaderGraph.AddNode(std::move(masterNode));
            m_ShaderGraph.SetMasterNodeId(masterId);

            auto colorNode = std::make_unique<ShaderGraph::ColorNode>(0);
            uint32 colorId = m_ShaderGraph.AddNode(std::move(colorNode));
            auto* color = m_ShaderGraph.GetNode(colorId);
            if (color) {
                color->SetPosition(50, 200);
            }

            auto fresnelNode = std::make_unique<ShaderGraph::FresnelNode>(0);
            uint32 fresnelId = m_ShaderGraph.AddNode(std::move(fresnelNode));
            auto* fresnel = m_ShaderGraph.GetNode(fresnelId);
            if (fresnel) {
                fresnel->SetPosition(50, 350);
            }

            auto multiplyNode = std::make_unique<ShaderGraph::MultiplyNode>(0);
            uint32 mulId = m_ShaderGraph.AddNode(std::move(multiplyNode));
            auto* mul = m_ShaderGraph.GetNode(mulId);
            if (mul) {
                mul->SetPosition(350, 250);
            }

            auto* master = m_ShaderGraph.GetNode(masterId);
            if (master) {
                master->SetPosition(600, 200);
            }

            // Connect: Color -> Multiply.A, Fresnel -> Multiply.B, Multiply -> Master.Albedo
            if (color && fresnel && mul && master) {
                m_ShaderGraph.AddLink(colorId, color->GetOutputPins()[0].id,
                                     mulId, mul->GetInputPins()[0].id);
                m_ShaderGraph.AddLink(fresnelId, fresnel->GetOutputPins()[0].id,
                                     mulId, mul->GetInputPins()[1].id);
                m_ShaderGraph.AddLink(mulId, mul->GetOutputPins()[0].id,
                                     masterId, master->GetInputPins()[0].id);
            }
        }

        m_MenuBar.ConsumeResetLayoutSignal();
    }

    void EngineEditor::OnUpdate(float32 dt) {
        Scene* scene = m_SceneManager.GetScene();
        if (!scene) return;
        if (m_SceneManager.IsPlaying()) scene->Update(dt);
        else if (m_SceneManager.IsPaused() && m_SceneManager.IsStepRequested()) {
            scene->Update(dt);
            m_SceneManager.ConsumeStepRequest();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 主 UI 渲染 — 全屏 DockSpace 底座 + 菜单栏 + 工具栏 + 所有面板
    // ═══════════════════════════════════════════════════════════════
    //
    // 架构：
    //   1. EditorRootWindow — 全屏无边框窗口（底座），包含 MenuBar
    //   2. ##ToolbarStrip — 在底座内的固定高度子窗口（不参与停靠）
    //   3. MainDockSpace — 底座内的 DockSpace 节点（面板可停靠）
    //   4. 各面板（Viewport / Hierarchy / Inspector / Console...）
    //
    // 为确保 Docking 正常工作：
    //   - 底座窗口不可有 ImGuiWindowFlags_NoDocking
    //   - DockSpace ID 必须固定（"MainDockSpace"）
    //   - 各面板的 ImGui::Begin() 窗口名必须固定
    //   - imgui.ini 保存停靠状态
    // ═══════════════════════════════════════════════════════════════

    void EngineEditor::OnImGui() {
        // ── 底座窗口：覆盖全工作区、无边框、包含菜单栏 ──
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        // 【关键】不要加 ImGuiWindowFlags_NoDocking！
        // 否则 DockSpace 节点将无法在此窗口内创建，Docking 完全失效。
        ImGuiWindowFlags rootFlags =
            ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        // 固定窗口名 "EditorRootWindow" — 不要包含 ## 前缀，
        // 否则 imgui.ini 无法正确保存该窗口的布局状态
        static bool dockspaceOpen = true;
        ImGui::Begin("EditorRootWindow", &dockspaceOpen, rootFlags);
        ImGui::PopStyleVar(3);

        // ── 1. 内嵌菜单栏 ──
        if (ImGui::BeginMenuBar()) {
            m_MenuBar.OnMenuBar();
            ImGui::EndMenuBar();
        }

        // ── 2. 工具栏（固定高度子窗口，不参与停靠） ──
        {
            const float tbH = 34.0f;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
            ImGui::BeginChild("##ToolbarStrip", ImVec2(0, tbH),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar);
            m_Toolbar.OnImGui();
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        // ── 3. 核心 DockSpace 节点 ──
        // 必须在底座窗口（EditorRootWindow）内调用 DockSpace，
        // 这样所有面板才能停靠到这个根底座。
        // ID "MainDockSpace" 固定不变，imgui.ini 自动保存用户布局。
        const ImGuiID dockspaceID = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        // ── 4. 渲染所有面板 ──
        // 每个面板的 ImGui::Begin() 窗口名必须固定，
        // imgui.ini 通过窗口名 + DockSpace ID 记忆停靠位置
        if (m_Visibility.viewport) {
            // ViewportPanel::OnImGui 内部调用 ImGui::Begin("Viewport")
            m_Viewport.OnImGui();
        }
        if (m_Visibility.inspector && m_Inspector) {
            // InspectorPanel::OnImGui 内部调用 ImGui::Begin("Inspector")
            m_Inspector->OnImGui();
        }
        if (m_Visibility.console && m_Console) {
            // ConsolePanel::OnImGui 内部调用 ImGui::Begin("Console")
            m_Console->OnImGui();
        }
        if (m_Visibility.performance && m_Performance) {
            m_Performance->OnImGui();
        }

        // ── 可选面板 ──
        if (m_Visibility.contentBrowser) m_ContentBrowser.OnImGui();
        if (m_Visibility.assetBrowser)   m_AssetBrowser.OnImGui();
        if (m_Visibility.depGraph)       m_DepGraph.OnImGui();

        // ── 场景面板 — 三种视图 ──
        // 根据 PanelVisibility 决定渲染哪个/哪些窗口
        if (m_Visibility.scenePanelTabbed) {
            // 合并模式：所有场景面板在一个 Tab 窗口中
            // 首次使用时注册所有面板指针（m_SceneHierarchy 可能稍后才注册）
            static bool panelsRegistered = false;
            if (!panelsRegistered && m_SceneHierarchy) {
                m_ScenePanel.RegisterPanels(m_SceneHierarchy,
                                             &m_SceneViewerPanel,
                                             &m_SceneManagerPanel);
                panelsRegistered = true;
            }
            m_ScenePanel.OnImGui();
        } else {
            // 独立模式：三个面板各自独立渲染
            if (m_Visibility.sceneHierarchy) {
                m_SceneHierarchy->OnImGui();
            }
            if (m_Visibility.sceneViewerPanel) {
                m_SceneViewerPanel.OnImGui();
            }
            if (m_Visibility.sceneManager) {
                m_SceneManagerPanel.OnImGui();
            }
        }

        // ── View Mode Debug 面板 ──
        // 由 ViewModePanel 管理其可见性

        // ── 编辑器设置面板（变换/吸附/书签等） ──
        // 由 DrawEditorSettingsWindow 管理可见性

        // ── 性能统计悬浮窗（使用 EditorTools 中的自由函数） ──
        // 需要渲染上下文数据填充
        if (auto* ctx = m_App ? m_App->GetRenderContext() : nullptr) {
            float fps = (ImGui::GetIO().DeltaTime > 0.0f) ? 1.0f / ImGui::GetIO().DeltaTime : 0.0f;
            float frameTime = ImGui::GetIO().DeltaTime * 1000.0f;
            uint32 dc = ctx->GetAndResetDrawCallCount();
            uint32 verts = ctx->GetAndResetVertexCount();
            uint32 tris = ctx->GetAndResetTriangleCount();
            uint64 vram = ctx->GetTextureVRAMBytes() + ctx->GetBufferVRAMBytes();
            DrawStatsOverlay(m_StatsCfg, fps, frameTime, dc, verts, tris, vram);
        }

        // ── 书签快捷键 ──
        HandleCameraBookmarkShortcuts(m_Viewport.GetCamera());

        // ── Renderer Debug ──
        if (m_Visibility.rendererDebug) {
            ImGui::Begin("Renderer Debug", &m_Visibility.rendererDebug);
            if (auto* ctx = m_App ? m_App->GetRenderContext() : nullptr) {
                ImGui::Text("Draw Calls:  %u", ctx->GetAndResetDrawCallCount());
                ImGui::Text("Vertices:    %u", ctx->GetAndResetVertexCount());
                ImGui::Text("Triangles:   %u", ctx->GetAndResetTriangleCount());
            }
            ImGui::End();
        }

        // ── 工具面板（通过 Tools 菜单触发） ──
        if (m_Visibility.shaderGraph) {
            m_ShaderGraphPanel.OnImGui();
        }
        if (m_Visibility.vfxEditor) {
            m_VFXGraphPanel.OnImGui();
        }
        if (m_Visibility.animationEditor) {
            ImGui::Begin("Animation Editor", &m_Visibility.animationEditor);
            ImGui::Text("Animation Editor - Coming Soon");
            ImGui::End();
        }

        if (m_ShowDockingDemo) ImGui::ShowDemoWindow(&m_ShowDockingDemo);

        ImGui::End(); // EditorRootWindow
    }

    // ============================================================
    // Gizmo 渲染
    // ============================================================

    void EngineEditor::DrawGizmo(const glm::mat4& viewMatrix,
                                 const glm::mat4& projMatrix) {
        auto selected = m_SelectedObject.lock();
        if (!selected) return;

        auto& transform = selected->GetTransform();
        const Mat4& worldMat = transform.GetWorldMatrix();
        glm::mat4 model = glm::make_mat4(worldMat.Data());

        ImGuizmo::OPERATION op;
        switch (m_Toolbar.GetGizmoMode()) {
            case 0:  op = ImGuizmo::TRANSLATE; break;
            case 1:  op = ImGuizmo::ROTATE;    break;
            case 2:  op = ImGuizmo::SCALE;     break;
            default: op = ImGuizmo::TRANSLATE; break;
        }

        ImGuizmo::MODE mode = m_Toolbar.IsGizmoLocal()
                              ? ImGuizmo::LOCAL
                              : ImGuizmo::WORLD;

        bool useSnap = m_Toolbar.IsSnapEnabled();
        float snapVals[3] = { m_Toolbar.GetSnapValue(), m_Toolbar.GetSnapValue(), m_Toolbar.GetSnapValue() };

        ImGuizmo::Manipulate(glm::value_ptr(viewMatrix),
                             glm::value_ptr(projMatrix),
                             op, mode,
                             glm::value_ptr(model),
                             nullptr,
                             useSnap ? snapVals : nullptr);

        if (ImGuizmo::IsUsing()) {
            glm::vec3 newPos, newScale, newSkew;
            glm::quat newRot;
            glm::vec4 newPerspective;
            glm::decompose(model, newScale, newRot, newPos, newSkew, newPerspective);
            glm::vec3 euler = glm::eulerAngles(newRot);
            transform.SetPosition(Vec3(newPos.x, newPos.y, newPos.z));
            transform.SetRotation(Vec3(glm::degrees(euler.x), glm::degrees(euler.y), glm::degrees(euler.z)));
            transform.SetScale(Vec3(newScale.x, newScale.y, newScale.z));
        }
    }

    // ============================================================
    // 包裹窗口（供外部独立调用）
    // ============================================================

    void EngineEditor::DrawSceneHierarchyWindow() {
        if (m_SceneHierarchy) { ImGui::Begin("Scene Hierarchy"); m_SceneHierarchy->OnImGui(); ImGui::End(); }
    }
    void EngineEditor::DrawInspectorWindow() {
        if (m_Inspector) { ImGui::Begin("Inspector"); m_Inspector->OnImGui(); ImGui::End(); }
    }
    void EngineEditor::DrawConsoleWindow() {
        if (m_Console) { ImGui::Begin("Console"); m_Console->OnImGui(); ImGui::End(); }
    }
    void EngineEditor::DrawPerformanceWindow() {
        if (m_Performance) { ImGui::Begin("Performance"); m_Performance->OnImGui(); ImGui::End(); }
    }
    void EngineEditor::DrawContentBrowserWindow() { m_ContentBrowser.OnImGui(); }
    void EngineEditor::DrawAssetBrowserWindow()   { m_AssetBrowser.OnImGui(); }
    void EngineEditor::DrawDepGraphWindow()       { m_DepGraph.OnImGui(); }
    void EngineEditor::DrawViewportPanel()         { m_Viewport.OnImGui(); }

} // namespace Engine