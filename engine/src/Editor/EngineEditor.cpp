#include "Engine/Editor/EngineEditor.h"

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

        // ── 工具栏/菜单栏回调 → EditorSceneManager Play/Stop/Pause/Step ──
        // 引擎核心场景运行状态由 EditorSceneManager 统一管理，
        // Toolbar 和 MainMenuBar 共享同一组回调。

        auto playFn  = [this]() { m_SceneManager.Play(); };
        auto stopFn  = [this]() { m_SceneManager.Stop(); };
        auto pauseFn = [this]() { m_SceneManager.TogglePause(); };
        auto stepFn  = [this]() { m_SceneManager.StepFrame(); };

        // ── MainMenuBar 播放控制 ──
        m_MenuBar.SetPlayCallback(playFn);
        m_MenuBar.SetStopCallback(stopFn);
        m_MenuBar.SetPauseCallback(pauseFn);
        m_MenuBar.SetStepCallback(stepFn);

        // ── Toolbar 播放控制 ──
        m_Toolbar.SetPlayCallback(playFn);
        m_Toolbar.SetStopCallback(stopFn);
        m_Toolbar.SetPauseCallback(pauseFn);
        m_Toolbar.SetStepCallback(stepFn);

        // ── EditorSceneManager 状态变更 → 同步 Toolbar 状态 ──
        m_SceneManager.SetStateChangeCallback([this](EditorState newState) {
            switch (newState) {
                case EditorState::Edit:
                    m_Toolbar.SetPlayState(Toolbar::PlayState::Stopped);
                    break;
                case EditorState::Play:
                    m_Toolbar.SetPlayState(Toolbar::PlayState::Playing);
                    break;
                case EditorState::Pause:
                    m_Toolbar.SetPlayState(Toolbar::PlayState::Paused);
                    break;
            }
        });

        // ── 视口模式变更回调 → 通知渲染上下文 ──
        m_Toolbar.SetViewModeCallback([this](int newMode) {
            if (auto* ctx = m_App ? m_App->GetRenderContext() : nullptr) {
                switch (newMode) {
                    case 0: // Solid → Normal
                        ctx->SetViewMode(ViewMode::Normal);
                        break;
                    case 1: // Wireframe
                        ctx->SetViewMode(ViewMode::Wireframe);
                        break;
                    case 2: // Lighting Only
                        ctx->SetViewMode(ViewMode::LightingOnly);
                        break;
                    default:
                        ctx->SetViewMode(ViewMode::Normal);
                        break;
                }
            }
        });

        // ── 布局重置回调 ──
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
        // ── 根据编辑器状态驱动场景更新 ──
        // EditorSceneManager 管理场景运行状态：
        //   - Edit  ：跳过场景更新（物理/脚本暂停）
        //   - Play  ：全速驱动 Scene::Update(dt)
        //   - Pause ：仅有步进请求时执行单帧更新
        // 场景指针通过 SetScene() 从外部注入。
        Scene* scene = m_SceneManager.GetScene();
        if (!scene) return;

        if (m_SceneManager.IsPlaying()) {
            scene->Update(dt);
        } else if (m_SceneManager.IsPaused() && m_SceneManager.IsStepRequested()) {
            scene->Update(dt);
            // 消费步进请求，避免每帧重复执行
            m_SceneManager.ConsumeStepRequest();
        }
    }

    void EngineEditor::OnImGui() {
        // ═══════════════════════════════════════════════════════════
        // 全屏 DockSpace 容器（根窗口）
        // ═══════════════════════════════════════════════════════════
        //
        // 此函数作为 Editor UI 的根容器。它创建了一个覆盖整个主视口的
        // 全屏无边框窗口，内嵌菜单栏和 DockSpace。
        //
        // 架构：
        //   1. "DockSpace" 窗口（全屏，无标题栏/边框/滚动条）
        //      → 包含 MenuBar 和 DockSpace 节点
        //   2. 所有编辑面板（Viewport, Hierarchy, Inspector...）
        //      作为子窗口被 DockSpace 系统管理
        //
        // 调用者约定：
        //   - 当 m_App->m_UseEngineEditorDockspace == true 时，
        //     Application::Run() 会跳过外层的 DockspaceBuilder，
        //     EngineEditor 自己负责创建 DockSpace 容器。
        //
        // 修复的问题：
        //   - 菜单栏被 DockSpace 窗口遮挡无法点击（Z-order 冲突）
        //   - 面板无法拖拽停靠（缺少全屏 DockSpace 容器）
        // ===========================================================

        // ── 1. 设置全屏 DockSpace 窗口属性 ──
        static bool dockspace_open = true;
        const bool  opt_fullscreen = true;

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar |
                                        ImGuiWindowFlags_NoDocking;

        if (opt_fullscreen) {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar |
                            ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus |
                            ImGuiWindowFlags_NoNavFocus;
        }

        // ── 2. 窗口内边距归零 ──
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        // ── 3. 创建 DockSpace 根窗口 ──
        // 窗口名称 "##DockSpace" 以 ## 开头，在标题栏不显示文本
        ImGui::Begin("##EditorDockSpace", &dockspace_open, window_flags);
        ImGui::PopStyleVar(); // 恢复 WindowPadding

        if (opt_fullscreen)
            ImGui::PopStyleVar(2); // 恢复 WindowRounding 和 WindowBorderSize

        // ── 4. 提交 DockSpace 节点 ──
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGuiID dockspace_id = ImGui::GetID("EditorInternalDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
                             ImGuiDockNodeFlags_None);
        }

        // ── 5. 内嵌菜单栏（解决菜单点击无响应的问题） ──
        // 菜单栏属于 DockSpace 根窗口的一部分，Z-order 正确，
        // 不会被任何其他窗口遮挡。
        if (ImGui::BeginMenuBar()) {
            // 使用 OnMenuBar() 而非 OnImGui()，后者会调用
            // BeginMainMenuBar() + EndMainMenuBar() 创建独立顶层菜单栏。
            m_MenuBar.OnMenuBar();

            // ── 5a. 菜单栏右侧信息 ──
            ImGui::SameLine(ImGui::GetWindowWidth() - 160.0f);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               "Engine Editor v0.1");

            ImGui::EndMenuBar();
        }

        // ═══════════════════════════════════════════════════════════
        // 6. 编辑面板（由 DockSpace 管理布局，用户可自由拖拽停靠）
        // ═══════════════════════════════════════════════════════════

        // 6a. 工具栏（浮动在 DockSpace 内的小窗口）
        ImGui::Begin("##Toolbar", nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoDocking); // 工具栏不参与停靠
        m_Toolbar.OnImGui();
        ImGui::End();

        // 6b. Scene Hierarchy
        if (m_Visibility.sceneHierarchy && m_SceneHierarchy) {
            ImGui::Begin("Scene Hierarchy", &m_Visibility.sceneHierarchy);
            m_SceneHierarchy->OnImGui();
            ImGui::End();
        }

        // 6c. Viewport（中央 3D/2D 视口）
        if (m_Visibility.viewport) {
            m_Viewport.OnImGui();
        }

        // 6d. Inspector
        if (m_Visibility.inspector && m_Inspector) {
            ImGui::Begin("Inspector", &m_Visibility.inspector);
            m_Inspector->OnImGui();
            ImGui::End();
        }

        // 6e. Console
        if (m_Visibility.console && m_Console) {
            ImGui::Begin("Console", &m_Visibility.console);
            m_Console->OnImGui();
            ImGui::End();
        }

        // 6f. Performance
        if (m_Visibility.performance && m_Performance) {
            ImGui::Begin("Performance", &m_Visibility.performance);
            m_Performance->OnImGui();
            ImGui::End();
        }

        // 6g. Content Browser（面板自身管理 Begin/End）
        if (m_Visibility.contentBrowser) {
            m_ContentBrowser.OnImGui();
        }

        // 6h. Asset Browser（面板自身管理 Begin/End）
        if (m_Visibility.assetBrowser) {
            m_AssetBrowser.OnImGui();
        }

        // 6i. Dependency Graph（面板自身管理 Begin/End）
        if (m_Visibility.depGraph) {
            m_DepGraph.OnImGui();
        }

        // ── 7. ImGui Demo（按需） ──
        if (m_ShowDockingDemo) {
            ImGui::ShowDemoWindow(&m_ShowDockingDemo);
        }

        // ── 8. 结束 DockSpace 根窗口 ──
        ImGui::End();
    }

    // ═══════════════════════════════════════════════════════════════
    // Gizmo 渲染 — 在视口内绘制 ImGuizmo 操作手柄
    // ═══════════════════════════════════════════════════════════════
    //
    // 在 Viewport 的 OnImGui() 渲染完 FBO 纹理后调用。
    // 需要相机 view/projection 矩阵和选中物体的变换矩阵。
    // ==============================================================

    void EngineEditor::DrawGizmo(const glm::mat4& viewMatrix,
                                 const glm::mat4& projMatrix) {
        // 没有选中物体 → 跳过 Gizmo 渲染
        auto selected = m_SelectedObject.lock();
        if (!selected) return;

        // 获取选中物体的 Transform 组件（引用，非指针）
        auto& transform = selected->GetTransform();

        // 从 TransformComponent 获取世界矩阵
        const Mat4& worldMat = transform.GetWorldMatrix();
        glm::mat4 model = glm::make_mat4(worldMat.Data());

        // 读取工具栏 Gizmo 模式
        ImGuizmo::OPERATION op;
        switch (m_Toolbar.GetGizmoMode()) {
            case 0:  op = ImGuizmo::TRANSLATE; break;
            case 1:  op = ImGuizmo::ROTATE;    break;
            case 2:  op = ImGuizmo::SCALE;     break;
            default: op = ImGuizmo::TRANSLATE;  break;
        }

        ImGuizmo::MODE mode = m_Toolbar.IsGizmoLocal()
                              ? ImGuizmo::LOCAL
                              : ImGuizmo::WORLD;

        // ── 准备 delta 矩阵（用于 ImGuizmo 的增量输出） ──
        // 如果是 ROTATE 模式且为 LOCAL，需要为 ImGuizmo 提供一个单位矩阵
        // 以便它正确应用旋转增量
        glm::mat4 deltaMatrix(1.0f);
        bool useSnap = false;
        float snap[3] = { 0.5f, 0.5f, 0.5f }; // 吸附精度

        // 执行 ImGuizmo 操作
        ImGuizmo::Manipulate(glm::value_ptr(viewMatrix),
                             glm::value_ptr(projMatrix),
                             op, mode,
                             glm::value_ptr(model),
                             glm::value_ptr(deltaMatrix),
                             useSnap ? &snap[0] : nullptr);

        // 如果操作被修改，将结果写回 TransformComponent
        if (ImGuizmo::IsUsing()) {
            // 分解模型矩阵 → 位置、旋转（Euler 角）、缩放
            glm::vec3 newPos, newScale, newSkew;
            glm::quat newRot;
            glm::vec4 newPerspective;
            glm::decompose(model, newScale, newRot, newPos, newSkew, newPerspective);

            // glm::decompose 返回的是 quat，需要转为 Euler 角
            // 使用 glm::eulerAngles 获取 pitch/yaw/roll
            glm::vec3 euler = glm::eulerAngles(newRot); // radians

            // 写入 TransformComponent（使用 Euler 角）
            transform.SetPosition(Vec3(newPos.x, newPos.y, newPos.z));
            transform.SetRotation(Vec3(glm::degrees(euler.x),
                                       glm::degrees(euler.y),
                                       glm::degrees(euler.z)));
            transform.SetScale(Vec3(newScale.x, newScale.y, newScale.z));
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