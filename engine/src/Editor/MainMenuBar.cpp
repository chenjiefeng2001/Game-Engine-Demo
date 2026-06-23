#include "Engine/Editor/MainMenuBar.h"
#include "Engine/Core/EngineSettings.h"
#include <imgui.h>

namespace Engine {

    // 用于在 Preferences 窗口中暂存的设置副本
    // 只在 Preferences 打开时从 EngineSettings 加载一次，关闭时写回
    static EngineSettings::Graphics s_PendingGraphics;
    static EngineSettings::Audio    s_PendingAudio;
    static EngineSettings::Physics  s_PendingPhysics;

    void MainMenuBar::OnImGui() {
        if (!ImGui::BeginMainMenuBar()) return;
        OnMenuBar();
        // 右侧状态信息
        ImGui::SameLine(ImGui::GetWindowWidth() - 160.0f);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Engine Editor v0.1");
        ImGui::EndMainMenuBar();
    }

    /**
     * @brief 在窗口菜单栏内部渲染菜单（在 BeginMenuBar/EndMenuBar 之间调用）
     *
     * 与 OnImGui() 共享相同的菜单绘制逻辑，但：
     *   - 不调用 BeginMainMenuBar() / EndMainMenuBar()
     *   - 不添加右侧状态文本（由外部窗口决定）
     *   - 适用于全屏 DockSpace 窗口内嵌菜单栏的架构
     */
    void MainMenuBar::OnMenuBar() {
        DrawFileMenu();
        DrawEditMenu();
        DrawSceneMenu();
        DrawViewMenu();
        DrawToolsMenu();
        DrawHelpMenu();

        // ── 持久对话框（菜单关闭后仍需渲染） ──
        DrawPreferencesWindow();

        // ── ImGui 内置调试窗口（由 Tools 菜单触发） ──
        if (m_ShowDemo) {
            ImGui::ShowDemoWindow(&m_ShowDemo);
        }
        if (m_ShowMetrics) {
            ImGui::ShowMetricsWindow(&m_ShowMetrics);
        }
        if (m_ShowStackTool) {
            ImGui::ShowIDStackToolWindow(&m_ShowStackTool);
        }
    }

    // ============================================================
    // File — 文件操作
    // ============================================================

    void MainMenuBar::DrawFileMenu() {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene",    "Ctrl+N")) {
                if (m_NewSceneCallback) m_NewSceneCallback();
            }

            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                // 回调接受文件路径字符串
                // 实际文件对话框由 EngineEditor 或外部分配的 OpenScenePathCallback 处理
                if (m_OpenSceneCallback) {
                    // 传递空字符串表示"由回调自己打开文件对话框"的约定
                    m_OpenSceneCallback("");
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save Scene",   "Ctrl+S")) {
                if (m_SaveSceneCallback) m_SaveSceneCallback();
            }

            if (ImGui::MenuItem("Save As...",   "Ctrl+Shift+S")) {
                if (m_SaveAsCallback) m_SaveAsCallback();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                if (m_ExitCallback) m_ExitCallback();
            }
            ImGui::EndMenu();
        }
    }

    // ============================================================
    // Edit — 编辑操作（撤消/重做 + 偏好设置）
    // ============================================================

    void MainMenuBar::DrawEditMenu() {
        if (ImGui::BeginMenu("Edit")) {
            // Undo/Redo 菜单项的启用状态由 UndoState 驱动
            // EngineEditor 或其他系统通过 SetCanUndo/SetCanRedo 更新
            bool canUndo = m_UndoState.canUndo;
            bool canRedo = m_UndoState.canRedo;

            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) {
                if (m_UndoCallback) m_UndoCallback();
            }

            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo)) {
                if (m_RedoCallback) m_RedoCallback();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Preferences...")) {
                m_ShowPreferences = true;
            }
            ImGui::EndMenu();
        }
    }

    // ============================================================
    // Scene — 场景播放控制
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
    // View — 面板可见性控制 + 布局重置
    // ============================================================

    void MainMenuBar::DrawViewMenu() {
        if (!m_Visibility) return;

        if (ImGui::BeginMenu("View")) {
            // 面板可见性切换（ImGui MenuItem 自动绑定布尔值）
            ImGui::MenuItem("Scene Hierarchy",  nullptr, &m_Visibility->sceneHierarchy);
            ImGui::MenuItem("Inspector",         nullptr, &m_Visibility->inspector);
            ImGui::MenuItem("Console",           nullptr, &m_Visibility->console);
            ImGui::MenuItem("Performance",       nullptr, &m_Visibility->performance);
            ImGui::MenuItem("Content Browser",   nullptr, &m_Visibility->contentBrowser);
            ImGui::MenuItem("Asset Browser",     nullptr, &m_Visibility->assetBrowser);
            ImGui::MenuItem("Dependency Graph",  nullptr, &m_Visibility->depGraph);
            ImGui::MenuItem("Viewport",          nullptr, &m_Visibility->viewport);

            // 渲染调试面板
            ImGui::MenuItem("Renderer Debug", nullptr, &m_Visibility->rendererDebug);

            ImGui::Separator();

            // 布局重置：通过信号机制通知 EngineEditor 消费
            if (ImGui::MenuItem("Reset Layout")) {
                m_ResetLayoutRequested = true;
                // 注意：实际的布局重置由 EngineEditor 在 OnImGui() 中
                // 检查 ConsumeResetLayoutSignal() 后触发
            }
            ImGui::EndMenu();
        }
    }

    // ============================================================
    // Tools — ImGui 内置调试工具
    // ============================================================

    void MainMenuBar::DrawToolsMenu() {
        if (ImGui::BeginMenu("Tools")) {
            // ImGui Demo 窗口
            ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemo);

            // ImGui Metrics/Debugger（性能分析 + 窗口列表）
            ImGui::MenuItem("Metrics/Debugger", nullptr, &m_ShowMetrics);

            // ImGui Stack Tool（控件堆栈追踪）
            ImGui::MenuItem("Stack Tool", nullptr, &m_ShowStackTool);

            // 预留
            // ImGui::MenuItem("Style Editor", nullptr, nullptr, false);

            ImGui::EndMenu();
        }
    }

    // ============================================================
    // Help — 关于
    // ============================================================

    void MainMenuBar::DrawHelpMenu() {
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                ImGui::OpenPopup("About Engine");
            }
            ImGui::EndMenu();
        }

        // ── About 弹窗 ──
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

    // ============================================================
    // Preferences — 偏好设置窗口
    // ============================================================

    void MainMenuBar::DrawPreferencesWindow() {
        if (!m_ShowPreferences) return;

        // Preferences 窗口打开时，从 EngineSettings 加载当前值
        static bool initialized = false;
        if (!initialized) {
            EngineSettings settings;
            settings.Load("assets/config/engine_settings.json");
            s_PendingGraphics = settings.GetGraphics();
            s_PendingAudio    = settings.GetAudio();
            s_PendingPhysics  = settings.GetPhysics();
            initialized = true;
        }

        ImGui::Begin("Preferences", &m_ShowPreferences, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Engine Settings");

        // ── Graphics 设置 ──
        if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            ImGui::InputInt("Window Width",  &s_PendingGraphics.windowWidth);
            ImGui::InputInt("Window Height", &s_PendingGraphics.windowHeight);
            ImGui::Checkbox("Fullscreen", &s_PendingGraphics.fullscreen);
            ImGui::Checkbox("VSync",      &s_PendingGraphics.vsync);
            ImGui::InputInt("Target FPS", &s_PendingGraphics.targetFPS);
            ImGui::SliderFloat("Render Scale", &s_PendingGraphics.renderScale, 0.5f, 2.0f, "%.2f");
            ImGui::Checkbox("Enable PostFX", &s_PendingGraphics.enablePostFX);
            ImGui::InputInt("Shadow Map Size", &s_PendingGraphics.shadowMapSize);

            ImGui::Unindent();
        }

        // ── Audio 设置 ──
        if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            ImGui::SliderFloat("Master Volume", &s_PendingAudio.masterVolume, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Music Volume",  &s_PendingAudio.musicVolume, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("SFX Volume",    &s_PendingAudio.sfxVolume,   0.0f, 1.0f, "%.2f");
            ImGui::InputInt("Audio Channels",   &s_PendingAudio.audioChannels);

            ImGui::Unindent();
        }

        // ── Physics 设置 ──
        if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            ImGui::InputInt("Velocity Iterations", &s_PendingPhysics.velocityIterations);
            ImGui::InputInt("Position Iterations", &s_PendingPhysics.positionIterations);
            ImGui::SliderFloat("Gravity Scale", &s_PendingPhysics.gravityScale, 0.0f, 5.0f, "%.2f");

            ImGui::Unindent();
        }

        ImGui::Separator();

        // ── 操作按钮 ──
        if (ImGui::Button("Save & Apply")) {
            EngineSettings settings;
            settings.Load("assets/config/engine_settings.json");
            settings.SetGraphics(s_PendingGraphics);
            settings.SetAudio(s_PendingAudio);
            settings.SetPhysics(s_PendingPhysics);
            settings.Save("assets/config/engine_settings.json");
            m_ShowPreferences = false;
            initialized = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Restore Defaults")) {
            EngineSettings settings;
            settings.RestoreDefaults();
            s_PendingGraphics = settings.GetGraphics();
            s_PendingAudio    = settings.GetAudio();
            s_PendingPhysics  = settings.GetPhysics();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_ShowPreferences = false;
            initialized = false;
        }

        ImGui::End();
    }

} // namespace Engine
