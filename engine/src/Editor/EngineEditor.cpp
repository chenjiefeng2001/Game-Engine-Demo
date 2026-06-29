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
#include "Engine/OpenGL/OpenGLContext.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtx/matrix_decompose.hpp>

#include "Engine/Platform/FileDialog.h"

namespace Engine {

    // 在子对象层级中递归查找指定 GameObject 的 shared_ptr
    static std::shared_ptr<GameObject> FindInChildren(const std::shared_ptr<GameObject>& parent, GameObject* target) {
        if (parent.get() == target) return parent;
        for (const auto& child : parent->GetChildren()) {
            if (child.get() == target) return child;
            auto found = FindInChildren(child, target);
            if (found) return found;
        }
        return nullptr;
    }

    EngineEditor::EngineEditor() = default;
    EngineEditor::~EngineEditor() = default;

    void EngineEditor::RegisterSceneHierarchy(SceneHierarchyPanel* panel) {
        m_SceneHierarchy = panel;
        if (panel) {
            // 连接 Hierarchy 的选择信号 → EngineEditor 的统一选择回调
            panel->SetSelectionCallback([this](GameObject* obj) {
                OnSelectionChanged(obj);
            });
        }
    }

    // ── 统一选择回调（Hierarchy ↔ Inspector ↔ Viewport Gizmo 联动） ──
    void EngineEditor::OnSelectionChanged(GameObject* obj) {
        // 更新 Inspector 目标
        if (m_Inspector) {
            m_Inspector->SetTarget(obj);
        }

        // 更新 Gizmo 选中对象
        // 通过 Scene::GetObjects() 找到对应的 shared_ptr（因为 GameObject 生命周期由 Scene 管理）
        if (obj) {
            Scene* scene = m_SceneManager.GetScene();
            if (scene) {
                for (const auto& sharedObj : scene->GetObjects()) {
                    if (sharedObj.get() == obj) {
                        m_SelectedObject = sharedObj;
                        m_Viewport.SetSelectedObject(sharedObj);
                        break;
                    }
                    // 也在子对象中查找
                    auto found = FindInChildren(sharedObj, obj);
                    if (found) {
                        m_SelectedObject = found;
                        m_Viewport.SetSelectedObject(found);
                        break;
                    }
                }
            }
        } else {
            m_SelectedObject.reset();
            std::shared_ptr<GameObject> nullObj;
            m_Viewport.SetSelectedObject(nullObj);
        }

        // 同步摄像机焦点（用于聚焦等操作）
        if (obj) {
            m_Viewport.GetCamera().SetFocusPoint(obj->GetTransform().GetPosition());
        }
    }

    void EngineEditor::Init(Application* app) {
        m_App = app;
        if (!app) return;

        m_MenuBar.SetPanelVisibility(&m_Visibility);

        // ── 初始化主视口 OpenGL 上下文 ──
        {
            auto* ctx = app->GetRenderContext();
            if (ctx) {
                auto* oglCtx = static_cast<OpenGLContext*>(ctx);
                GladGLContext& gl = oglCtx->GetGL();
                m_Viewport.SetRenderContext(ctx);
                m_Viewport.SetGLContext(&gl);
                m_Viewport.InitResources(&app->GetFactory(),
                                          "assets/shaders/3d_lit.vert",
                                          "assets/shaders/3d_lit.frag");
            }
        }

        m_Viewport.SetResizeCallback([app](int32 w, int32 h) {
            if (auto* ctx = app->GetRenderContext()) {
                ctx->OnResize(w, h);
            }
        });

        // 设置场景渲染回调：由 Application 子类注入具体绘制逻辑
        // MRT 单次 Pass：Fragment Shader 同时输出颜色(location=0) 和 ID(location=1)
        m_Viewport.SetSceneRenderCallback([this, app](const float* viewProj16, const float* camPos3, bool isPicking) {
            if (m_SceneRenderInjector) {
                m_SceneRenderInjector(viewProj16, camPos3, isPicking);
            }
        });

        // ── 【核心修复】：闭环处理实体的右键创建与删除 ──
        m_Viewport.SetSceneCreateCallback([this](const std::string& type) {
            Scene* scene = m_SceneManager.GetScene();
            if (!scene) return;

            auto obj = std::make_shared<GameObject>("New " + type);

            // 计算在摄像机前方 5 米处的生成位置
            Vec3 pos = m_Viewport.GetCamera().GetPosition();
            Vec3 fwd = m_Viewport.GetCamera().GetForwardVector();
            obj->GetTransform().SetPosition(Vec3(pos.x + fwd.x * 5.0f,
                                                 pos.y + fwd.y * 5.0f,
                                                 pos.z + fwd.z * 5.0f));

            scene->AddObject(obj);
            OnSelectionChanged(obj.get()); // 自动选中新建的物体
        });

        m_Viewport.SetSceneDeleteCallback([this]() {
            auto selected = m_SelectedObject.lock();
            if (selected) {
                Scene* scene = m_SceneManager.GetScene();
                if (scene) {
                    scene->RemoveObject(selected.get());
                    OnSelectionChanged(nullptr); // 清空选中状态
                }
            }
        });

        // ── 3D 场景可视化叠加层（SceneViewerPanel → ViewportPanel 包围盒/标签） ──
        m_Viewport.SetLayerDrawCallback([this](EditorCamera* camera, float32 dt) {
            m_SceneViewerPanel.OnOverlay(dt, camera);
        });

        // ── 连接 Viewport 拾取回调（鼠标点击 → 查找 Entity ID → 发布选中事件） ──
        m_Viewport.SetPickCallback([this](float ndcX, float ndcY, int32 entityId) {
            (void)ndcX;
            (void)ndcY;
            Scene* scene = m_SceneManager.GetScene();
            if (!scene) return;

            if (entityId > 0) {
                // 在场景中通过 ID 查找对应对象
                GameObject* obj = scene->FindByID(static_cast<uint32>(entityId));
                if (obj) {
                    ENGINE_LOG_INFO("Viewport", "Selected Entity ID: {} - {}", entityId, obj->GetName());
                    EventBus::Publish(EntitySelectedEvent(obj));
                } else {
                    ENGINE_LOG_WARN("Viewport", "Entity ID {} not found in scene", entityId);
                }
            } else {
                // 点击背景（entityId = -1），清除选择
                EventBus::Publish(EntitySelectedEvent(nullptr));
            }
        });

        // ── 通过 EventBus 订阅拾取结果（解耦：Viewport → EngineEditor） ──
        m_PickSubscription = EventBus::Subscribe<EntitySelectedEvent>(
            [this](const EntitySelectedEvent& e) {
                if (e.Entity) {
                    OnSelectionChanged(e.Entity);
                } else {
                    OnSelectionChanged(nullptr);
                }
            }
        );

        // ── 通过 EventBus 订阅场景切换（解耦：EditorSceneManager → 所有面板） ──
        m_SceneSwitchSubscription = EventBus::Subscribe<SceneSwitchedEvent>(
            [this](const SceneSwitchedEvent& e) {
                if (m_SceneHierarchy) {
                    m_SceneHierarchy->SetScene(e.ActiveScene);
                }
            }
        );

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

        // ── 设置 PIE 面板切换回调 ──
        m_SceneManager.SetPanelSwitchCallback([this](Scene* activeScene) {
            // 切换 Hierarchy 的数据源
            if (m_SceneHierarchy) {
                m_SceneHierarchy->SetScene(activeScene);
            }

            // 切换 Inspector 目标（如果当前选中的对象在新场景中存在）
            if (m_Inspector) {
                auto selected = m_SelectedObject.lock();
                if (selected) {
                    // 按 ID 在激活场景中查找对应对象
                    GameObject* newTarget = activeScene ? activeScene->FindByID(selected->GetID()) : nullptr;
                    m_Inspector->SetTarget(newTarget);
                    if (!newTarget) {
                        m_SelectedObject.reset();
                    }
                } else {
                    m_Inspector->SetTarget(nullptr);
                }
            }

            // 更新 Viewport 的场景引用（用于 Gizmo 的选中对象查找）
            // 注：Viewport 只持有选中对象的 shared_ptr，场景切换后路径已失效，
            // 需要在 Play 前用 m_SelectedObject 重新匹配
            if (m_Inspector) {
                auto selected = m_SelectedObject.lock();
                if (selected && activeScene) {
                    GameObject* newTarget = activeScene->FindByID(selected->GetID());
                    if (newTarget) {
                        // 从新场景中重建 shared_ptr
                        for (const auto& obj : activeScene->GetObjects()) {
                            if (obj.get() == newTarget) {
                                m_SelectedObject = obj;
                                m_Viewport.SetSelectedObject(obj);
                                break;
                            }
                            auto found = FindInChildren(obj, newTarget);
                            if (found) {
                                m_SelectedObject = found;
                                m_Viewport.SetSelectedObject(found);
                                break;
                            }
                        }
                    } else {
                        m_SelectedObject.reset();
                        std::shared_ptr<GameObject> nullObj;
                        m_Viewport.SetSelectedObject(nullObj);
                    }
                }
            }
        });

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

        // ── 更新主视口（相机输入 + 场景渲染到 FBO） ──
        // 主视口始终视为焦点视口
        m_Viewport.OnUpdate(dt, true);
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

        // ── 2. 工具栏（自动高度子窗口，内容自适应单行/双行，不参与停靠） ──
        {
            // AutoResizeY 让子窗口高度严格等于 Toolbar 渲染的实际内容高度，
            // Toolbar 内部已改用固定 Y 偏移（而非 GetContentRegionAvail() 居中），
            // 因此不会出现按钮下沉或空位过多的问题。
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
            ImGui::BeginChild("##ToolbarStrip", ImVec2(0, 0),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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

        // ── 底部状态栏（实时渲染统计） ──
        {
            if (auto* ctx = m_App ? m_App->GetRenderContext() : nullptr) {
                m_StatusBar.SetFPS(ImGui::GetIO().Framerate);
                m_StatusBar.SetFrameTime(ImGui::GetIO().DeltaTime * 1000.0f);
                m_StatusBar.SetDrawCalls(ctx->GetAndResetDrawCallCount());
                m_StatusBar.SetTriangleCount(ctx->GetAndResetTriangleCount());
                uint64 vram = ctx->GetTextureVRAMBytes() + ctx->GetBufferVRAMBytes();
                m_StatusBar.SetMemoryUsage(static_cast<uint64>(vram / (1024 * 1024)), 1024);
            }
            m_StatusBar.OnImGui();
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