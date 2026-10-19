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
#include "Engine/Editor/UndoSystem.h"
#include "Engine/Editor/EditorLayoutSpec.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/IWindow.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Log.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "Engine/Editor/IconsFontAwesome6.h"
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

    void EngineEditor::RegisterInspector(InspectorPanel* panel) {
        m_Inspector = panel;
        if (panel) {
            // S4b 记录端接线：Inspector 属性修改经帧首快照对比生成
            // PropertyChangeCommand（Transform/Name 域），进入全局撤销栈
            panel->SetUndoCallback([panel](GameObject* obj) {
                if (obj) panel->FlushUndoFromSnapshot(obj);
            });
        }
    }

    void EngineEditor::SetExternalPlayback(std::function<bool()> play,
                                           std::function<void()> stop) {
        m_ExternalPlay = std::move(play);
        m_ExternalStop = std::move(stop);

        auto menuPlayFn = [this]() {
            const bool ok = m_ExternalPlay ? m_ExternalPlay() : m_SceneManager.Play();
            if (!ok) ENGINE_LOG_WARN("Editor", "[PIE] play not started (refused or failed)");
        };
        auto menuStopFn = [this]() {
            if (m_ExternalStop) m_ExternalStop(); else m_SceneManager.Stop();
        };

        m_MenuBar.SetPlayCallback(menuPlayFn);
        m_MenuBar.SetStopCallback(menuStopFn);
        m_MenuBar.SetPauseCallback({});
        m_MenuBar.SetStepCallback({});
        m_MenuBar.SetExternalPlaybackMode(true);

        // 工具栏走非乐观 PlayAction：以真实返回值置位
        m_Toolbar.SetPlayCallback([this]() -> bool {
            return m_ExternalPlay ? m_ExternalPlay() : m_SceneManager.Play();
        });
        m_Toolbar.SetStopCallback(menuStopFn);
        m_Toolbar.SetPauseCallback({});
        m_Toolbar.SetStepCallback({});
        // 外部播放的状态回写由宿主经 GetToolbar().SetPlayState 驱动
        //（GP01 会话的 onPlayStateChanged），不再依赖引擎 StateChangeCallback
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
                // Phase 7: Enable FBO readback for Avalonia texture interop
                m_Viewport.EnableReadback(true);
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

        // 默认接线：引擎内置 PIE（宿主可经 SetExternalPlayback 整体接管，
        // 届时以下 Play/Stop/Pause/Step 回调被覆盖，Pause/Step 隐藏）
        auto playFn  = [this]() -> bool { return m_SceneManager.Play(); };
        auto stopFn  = [this]() { m_SceneManager.Stop(); };
        auto pauseFn = [this]() { m_SceneManager.TogglePause(); };
        auto stepFn  = [this]() { m_SceneManager.StepFrame(); };

        m_MenuBar.SetPlayCallback([this]() {
            if (!m_SceneManager.Play())
                ENGINE_LOG_WARN("Editor", "[PIE] play not started (no scene or already playing)");
        });
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

        // ── S4b：Undo 三线打通 ──
        // 执行端：对象解析回调（按当前活跃场景 FindID，替代桩 nullptr）
        UndoManager::Get().SetObjectResolver([this](uint64 id) -> GameObject* {
            Scene* s = m_SceneManager.GetScene();
            return s ? s->FindByID(static_cast<uint32>(id)) : nullptr;
        });
        // UI 端：菜单 Undo/Redo 真实驱动全局栈
        m_MenuBar.SetUndoCallback([]() { UndoManager::Get().GetGlobalStack().Undo(); });
        m_MenuBar.SetRedoCallback([]() { UndoManager::Get().GetGlobalStack().Redo(); });

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

        // P1-c：Toolbar Reset Layout 按钮已摘除（与 View>Reset Layout 构成
        // 同类双入口，GP-DX-011 CAT 族）—— 布局重置唯一入口 = View 菜单。

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
        // S4b：Undo/Redo 菜单项启用状态每帧对齐全局栈真实深度
        {
            const auto& gs = UndoManager::Get().GetGlobalStack();
            m_MenuBar.SetCanUndo(gs.GetUndoCount() > 0);
            m_MenuBar.SetCanRedo(gs.GetRedoCount() > 0);
        }
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

        // ── 3.5 布局守卫 / 重置（P1-c 实装，GP1-DX 启动调查 2026-08-26）──
        // 两个触发源共用同一规范布局：
        //   a) View>Reset Layout 菜单信号；
        //   b) 引导条件：Viewport 窗口未停靠进 MainDockSpace —— 说明 imgui.ini
        //      缺失/失效/被撕裂（实测症状：整编辑器拆成十几个漂浮 OS 窗口、
        //      原生标题栏渲染图标为 "?"）。InitDockingLayout 自 P0 起从未
        //      实现即为此缺口。
        auto ApplyDefaultLayout = [&]() {
            ImGui::DockBuilderRemoveNode(dockspaceID);
            ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->WorkSize);

            ImGuiID mainNode = dockspaceID;
            const ImGuiID dockBottom = ImGui::DockBuilderSplitNode(
                mainNode, ImGuiDir_Down, kLayoutBottomFraction, nullptr, &mainNode);
            const ImGuiID dockRight = ImGui::DockBuilderSplitNode(
                mainNode, ImGuiDir_Right, kLayoutRightFraction, nullptr, &mainNode);
            const ImGuiID dockLeft = ImGui::DockBuilderSplitNode(
                mainNode, ImGuiDir_Left, kLayoutLeftFraction, nullptr, &mainNode);

            // 右列：上 Inspector / 下 GP01 生产三件套 Tab 组
            //（宿主级窗口，GP1-DX 启动调查：此前漂浮为独立 OS 窗口撕裂工作区）
            ImGuiID dockRightMut = dockRight;
            const ImGuiID dockProd = ImGui::DockBuilderSplitNode(
                dockRightMut, ImGuiDir_Down, kLayoutRightBottomFraction, nullptr, &dockRightMut);

            // 窗口名必须与各面板 Begin() 的实际标题一致（P1-c 已全部去图标）。
            // 登记表是 EditorLayoutSpec.h 的 kEditorDockEntries：漏登记**不会**产生
            // 任何编译或运行错误，只会静默退化成漂浮 OS 窗，所以
            // tests/test_editor_layout 会对 engine/src 做源码扫描来兜住这一类。
            const ImGuiID zoneNode[kEditorZoneCount] = {
                dockLeft, mainNode, dockBottom, dockRightMut, dockProd};
            for (size_t i = 0; i < kEditorDockEntryCount; ++i) {
                const int zone = static_cast<int>(kEditorDockEntries[i].Zone);
                ImGui::DockBuilderDockWindow(kEditorDockEntries[i].WindowTitle,
                                             zoneNode[zone]);
            }

            ImGui::DockBuilderFinish(dockspaceID);
            Log::Info("Editor layout rebuilt (bootstrap or user reset)");
        };

        // 引导/重置判定（GP1-DX 启动调查）：仅当 Viewport 窗口已存在且
        // 未停靠时才重建 —— 首帧窗口尚未创建，若此时判定会每次启动都
        // 误触发重建、绕过 imgui.ini 持久化（实测复现后修正）。
        const bool resetRequested = m_MenuBar.ConsumeResetLayoutSignal();
        bool viewportFloating = false;
        if (const ImGuiWindow* vpWin = ImGui::FindWindowByName("Viewport"))
            viewportFloating = (vpWin->DockNode == nullptr);

        if (resetRequested || viewportFloating) {
            ApplyDefaultLayout();
        }

        // ── 4. 渲染所有面板 ──
        // 每个面板的 ImGui::Begin() 窗口名必须固定，
        // imgui.ini 通过窗口名 + DockSpace ID 记忆停靠位置

        // 停靠面板可见性的**双向**同步。
        //
        // 背景：菜单项写的是 PanelVisibility::<field>，而面板的
        // ImGui::Begin(标题, &m_Visible) 里的关闭按钮写的是面板自己的
        // m_Visible —— 同一件事有两个 bool。历史上二者互不同步，后果是：
        //   · 只看菜单：Console 默认 m_Visible=false，OnImGui 开头早退，
        //     View > Console 成为死控件，规范布局里给它的 Tab 永不出现；
        //   · 只看面板：菜单项切了没有任何可见效果。
        // 双向同步让两者在每帧末恒等，关闭按钮与菜单项不再互相打架。
        //
        // SetVisible 只在**真正变化**时调用：ConsolePanel::SetVisible 在
        // visible 时会 FocusInput()，每帧无条件调用会让控制台输入框永久
        // 抢走键盘焦点。
        auto SyncPanelVisibility = [](bool& menuFlag, auto* panel, auto draw) {
            if (!panel) return;
            if (panel->IsVisible() != menuFlag)
                panel->SetVisible(menuFlag);
            if (!menuFlag) return;
            draw(panel);
            menuFlag = panel->IsVisible();   // 回读：关闭按钮的结果反映到菜单
        };

        if (m_Visibility.viewport) {
            // ViewportPanel::OnImGui 内部调用 ImGui::Begin("Viewport")
            m_Viewport.OnImGui();
        }
        if (m_Inspector) {
            // InspectorPanel::OnImGui 内部调用 ImGui::Begin("Inspector")
            SyncPanelVisibility(m_Visibility.inspector, m_Inspector,
                                      [](InspectorPanel* p) { p->OnImGui(); });
        }
        if (m_Console) {
            // ConsolePanel::OnImGui 内部调用 ImGui::Begin("Console")
            SyncPanelVisibility(m_Visibility.console, m_Console,
                                      [](ConsolePanel* p) { p->OnImGui(); });
        }
        if (m_Performance) {
            SyncPanelVisibility(m_Visibility.performance, m_Performance,
                                      [](PerformanceWindow* p) { p->OnImGui(); });
        }

        // ── 可选面板 ──
        // 同样走双向同步：此前三个都是裸调用 `if (m_Visibility.X) panel.OnImGui()`，
        // 而 ContentBrowserPanel 与 DependencyGraphPanel 的自身 m_Visible 默认
        // false，OnImGui 开头即早退 —— 于是 View > Content Browser 与
        // View > Dependency Graph 两个菜单项都是死的。
        SyncPanelVisibility(m_Visibility.contentBrowser, &m_ContentBrowser,
                            [](ContentBrowserPanel* p) { p->OnImGui(); });
        SyncPanelVisibility(m_Visibility.assetBrowser, &m_AssetBrowser,
                            [](AssetBrowserPanel* p) { p->OnImGui(); });
        SyncPanelVisibility(m_Visibility.depGraph, &m_DepGraph,
                            [](DependencyGraphPanel* p) { p->OnImGui(); });

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
            if (m_SceneHierarchy) {
                // SceneHierarchyPanel::OnImGui 内部调用 ImGui::Begin("Hierarchy")
                SyncPanelVisibility(m_Visibility.sceneHierarchy, m_SceneHierarchy,
                                          [](SceneHierarchyPanel* p) { p->OnImGui(); });
            }
            SyncPanelVisibility(m_Visibility.sceneViewerPanel, &m_SceneViewerPanel,
                                      [](SceneViewerPanel* p) { p->OnImGui(); });
            SyncPanelVisibility(m_Visibility.sceneManager, &m_SceneManagerPanel,
                                      [](SceneManagerPanel* p) { p->OnImGui(); });
        }

        // ── 尚未接入本帧循环的面板（2026-10-19 更正） ──
        // 此前这里有两段注释，声称「View Mode Debug 面板由 ViewModePanel 管理
        // 可见性」「编辑器设置面板由 DrawEditorSettingsWindow 管理可见性」。
        // 两句都不成立：
        //   · m_ViewModePanel 是成员（EngineEditor.h），但全仓**没有任何**
        //     m_ViewModePanel.OnImGui() 调用 —— 它从未被渲染；
        //   · DrawEditorSettingsWindow 已随整族死代码 Draw*Window 一并删除
        //     （该族 12 个成员零调用者，其中 4 个只有声明没有定义）。
        // 注释描述未发生的事，是这轮审计里「过时文档误导后续工作」的又一例
        // （GP1-DX-UI-Audit 的「窗口数 14→1」结论同样不成立）。
        // 若将来要接入，此处需要显式的绘制调用 + 可见性归属，不能靠注释暗示。

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
        // 动画编辑器 — 菜单切换开/关
        if (m_Visibility.animationEditor) {
            m_AnimationEditorPanel.SetVisible(true);  // 重置可见性（克服关闭按钮的清零）
            m_AnimationEditorPanel.OnImGui();
        }

        if (m_ShowDockingDemo) ImGui::ShowDemoWindow(&m_ShowDockingDemo);

        ImGui::End(); // EditorRootWindow
    }

    // ============================================================
    // 曾经的「包裹窗口（供外部独立调用）」一整族 Draw*Window 已删除。
    //
    // 删除理由不是「暂时用不到」，而是它们**有害**：
    //   1. 整族 12 个成员零调用者 —— 真实渲染在 OnImGui() 里内联完成
    //      （见本文件 618-631，那里的注释明写「各面板 OnImGui 内部调用
    //      ImGui::Begin(...)」）。
    //   2. 包裹器在面板自己的 Begin 之外又套了一层同名 Begin，
    //      例如 DrawConsoleWindow 会在 ConsolePanel::OnImGui()（内部已
    //      Begin("Console")）外再 Begin("Console")。同名窗口二次 Begin
    //      会让 BeginCount=2、内容互相覆盖 —— 一旦有人把它接回去就出 bug。
    //   3. 其中 4 个（DrawSceneManagerWindow / DrawSceneViewerWindow /
    //      DrawViewModeWindow / DrawEditorSettingsWindow）**只有声明没有
    //      定义**，被调用即链接失败 —— 与已删除的 InitDockingLayout 同型。
    //
    // 需要在别处单独绘制某个面板时，直接调该面板的 OnImGui()。
    // ============================================================

} // namespace Engine