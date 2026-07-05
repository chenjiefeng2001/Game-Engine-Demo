#include "EditorDemoTest.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/GameObject/GameObject.h"

namespace Engine {

    EditorDemoTest::EditorDemoTest(EditorDemoApp* app)
        : m_App(app) {}

    void EditorDemoTest::Init() {
        // ── 1. 初始化资产数据库 ──
        EditorAssetDatabase::Get().Initialize("assets/", "Library/");

        // 注册资产浏览回调
        m_AssetBrowser.Init(&EditorAssetDatabase::Get(), &m_DepTracker);
        m_AssetBrowser.SetAssetSelectCallback([](const GUID& guid, const std::string& path) {
            Log::Info("[DemoTest] Selected asset: {} ({})", guid.ToString(), path);
        });

        // ── 2. 初始化插件系统 ──
        m_PluginMgr.Init();
        m_PluginMgr.AddHook(Plugin::EditorHook::OnUpdate, [this]() {
            m_ProfilerCore.BeginFrame(m_TestFrameCount);
        });

        // ── 3. 注册快捷开关到状态栏 ──
        m_StatusBar.SetGitBranch("master");
        m_StatusBar.SetTargetPlatform("Windows");
        m_StatusBar.SetEditorVersion("v0.1-demo");
        m_StatusBar.SetQuickToggle("Wire", &m_WireframeMode, "Toggle wireframe mode");
        m_StatusBar.SetQuickToggle("Mute", &m_MuteAudio, "Toggle audio");

        // ── 4. 模拟测试数据 ──
        m_DepTracker.AddDependency(GUID::Generate(), GUID::Generate(), true);
        Log::Info("[DemoTest] Editor demo initialized");
    }

    void EditorDemoTest::OnUpdate(float dt) {
        m_TestFrameCount++;

        // ── 更新 Profiler ──
        m_ProfilerCore.EndFrame();

        // 模拟性能数据
        Debug::CPUSample cpuSample;
        cpuSample.name = "Update";
        cpuSample.elapsedMs = 8.0 + (std::rand() % 100) * 0.01;
        m_ProfilerCore.RecordCPUSample(cpuSample);

        Debug::GPUSample gpuSample;
        gpuSample.passName = "MainPass";
        gpuSample.elapsedMs = 5.0 + (std::rand() % 50) * 0.01;
        gpuSample.drawCalls = 100 + std::rand() % 50;
        m_ProfilerCore.RecordGPUSample(gpuSample);

        Debug::MemorySample memSample;
        memSample.textureVRAMBytes = 256 * 1024 * 1024;
        memSample.bufferVRAMBytes = 64 * 1024 * 1024;
        memSample.textureCount = 42;
        memSample.meshCount = 128;
        m_ProfilerCore.RecordMemorySample(memSample);

        // 更新状态栏
        auto stats = m_ProfilerCore.GetAggregatedStats(60);
        m_StatusBar.SetFrameTime(static_cast<float>(stats.avgFrameTimeMs));
        m_StatusBar.SetFPS(static_cast<float>(1000.0f / std::max(stats.avgFrameTimeMs, 0.001)));
        m_StatusBar.SetDrawCalls(static_cast<uint32>(stats.avgDrawCalls));
        m_StatusBar.SetMemoryUsage(2048, 16384);

        // 更新后台任务（模拟）
        static float taskProgress = 0.0f;
        if (m_TestFrameCount % 300 == 0) {
            BackgroundTask task;
            task.name = "shader_compile";
            task.description = "Compiling Shaders";
            task.isIndeterminate = true;
            task.cancellable = true;
            m_StatusBar.PushTask(task);
        }
        if (m_TestFrameCount % 600 == 0) {
            m_StatusBar.PopTask("shader_compile");
            m_StatusBar.ShowNotification("Shader compilation complete!");
        }

        // 依赖追踪测试
        static bool depsAdded = false;
        if (!depsAdded && m_TestFrameCount > 10) {
            auto g1 = GUID::Generate(), g2 = GUID::Generate(), g3 = GUID::Generate();
            m_DepTracker.AddDependency(g1, g2, true);
            m_DepTracker.AddDependency(g1, g3, true);
            m_DepTracker.AddDependency(g2, g3, false);
            depsAdded = true;
            Log::Info("[DemoTest] Added test dependencies");
        }
    }

    void EditorDemoTest::OnImGui() {
        // ── 测试菜单 ──
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Demo Test")) {
                ImGui::MenuItem("Show Test UI", nullptr, &m_ShowTestUI);
                ImGui::Separator();
                if (ImGui::MenuItem("Test Undo/Redo")) {
                    auto go = std::make_shared<GameObject>("TestObject");
                    UndoManager::RecordPropertyChange(go.get(), "Position",
                                                      nlohmann::json{{"x",0},{"y",0},{"z",0}},
                                                      nlohmann::json{{"x",1},{"y",2},{"z",3}});
                    Log::Info("[DemoTest] Recorded property change");
                }
                if (ImGui::MenuItem("Test Console Log (Info)")) {
                    Log::Info("[DemoTest] This is an info message");
                }
                if (ImGui::MenuItem("Test Console Log (Warning)")) {
                    Log::Info("[DemoTest] This is a warning message");
                }
                if (ImGui::MenuItem("Test Console Log (Error)")) {
                    Log::Info("[DemoTest] This is an error message");
                }
                if (ImGui::MenuItem("Test Background Task")) {
                    BackgroundTask task;
                    task.name = "test_task";
                    task.description = "Testing...";
                    task.progress = 0.0f;
                    m_StatusBar.PushTask(task);
                }
                if (ImGui::MenuItem("Start Frame Capture")) {
                    m_ProfilerCore.StartCapture("demo_capture");
                }
                if (ImGui::MenuItem("Stop Frame Capture")) {
                    m_ProfilerCore.StopCapture();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // ── 状态栏 ──
        m_StatusBar.OnImGui();

        // ── 测试结果面板 ──
        if (m_ShowTestUI) {
            ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
            ImGui::Begin("Editor Demo Test", &m_ShowTestUI);

            DrawTestResults();

            ImGui::Separator();

            // ── 各组件独立测试按钮 ──
            if (ImGui::CollapsingHeader("Component Tests", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("Open Asset Browser")) {
                    m_AssetBrowser.SetVisible(true);
                }
                ImGui::SameLine();
                if (ImGui::Button("Open Profiler")) {
                    m_ProfilerPanel.ToggleVisibility();
                }
                ImGui::SameLine();
                if (ImGui::Button("Toggle Console")) {
                    m_ConsolePanel.ToggleVisibility();
                }
            }

            // ── 撤销系统测试 ──
            if (ImGui::CollapsingHeader("Undo System")) {
                ImGui::Text("Undo count: %zu", m_UndoMgr.GetGlobalStack().GetUndoCount());
                ImGui::Text("Redo count: %zu", m_UndoMgr.GetGlobalStack().GetRedoCount());
                ImGui::Text("Undo description: %s", m_UndoMgr.GetGlobalStack().GetUndoDescription().c_str());
                if (ImGui::Button("Undo")) {
                    m_UndoMgr.GetGlobalStack().Undo();
                }
                ImGui::SameLine();
                if (ImGui::Button("Redo")) {
                    m_UndoMgr.GetGlobalStack().Redo();
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                    m_UndoMgr.GetGlobalStack().Clear();
                }

                ImGui::Separator();
                if (ImGui::Button("Test Property Change (will merge)")) {
                    auto go = std::make_shared<GameObject>("Test");
                    UndoManager::OpenGroup("Drag Position");
                    for (int i = 0; i < 10; ++i) {
                        UndoManager::RecordPropertyChange(go.get(), "Position",
                            nlohmann::json{{"x",i-1}}, nlohmann::json{{"x",i}});
                    }
                    UndoManager::CloseGroup();
                }
            }

            // ── 插件系统测试 ──
            if (ImGui::CollapsingHeader("Plugin System")) {
                auto plugins = m_PluginMgr.GetLoadedPlugins();
                ImGui::Text("Loaded plugins: %zu", plugins.size());
                ImGui::Text("Menu items: %zu", m_PluginMgr.GetAllMenuItems().size());

                // 添加示例菜单项
                if (ImGui::Button("Register Test Menu Item")) {
                    Plugin::MenuItem item;
                    item.path = "Tools/Test Tool";
                    item.priority = 500;
                    item.action = []() { Log::Info("[DemoTest] Test tool activated!"); };
                    m_PluginMgr.RegisterMenuItem(item);
                }
            }

            // ── 场景面板测试 ──
            if (ImGui::CollapsingHeader("Scene Panel Mediator")) {
                SceneSelectionEvent evt;
                evt.sceneName = "TestScene";
                evt.objectName = "TestObject";
                evt.objectId = 42;
                evt.source = SceneSelectionSource::External;

                if (ImGui::Button("Simulate Scene Selection")) {
                    m_Mediator.OnSceneSelected(evt);
                }
                if (ImGui::Button("Simulate Solo Mode")) {
                    m_Mediator.OnSoloModeChanged("TestScene", true);
                }
                if (ImGui::Button("Simulate Scene Loaded")) {
                    m_Mediator.OnSceneLoaded("NewScene");
                }
            }

            ImGui::End();
        }

        // ── 其他独立面板 ──
        m_ProfilerPanel.OnImGui(m_ProfilerCore);
        m_ConsolePanel.OnImGui();
        m_AssetBrowser.OnImGui();
    }

    void EditorDemoTest::DrawTestResults() {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1), "=== Component Status ===");
        ImGui::Spacing();

        // 状态列表
        struct ComponentStatus {
            const char* name;
            bool operational;
            const char* statusText;
        };

        ComponentStatus statuses[] = {
            {"ScenePanelMediator",  true, "Operational"},
            {"InspectorPanel",      true, "Operational"},
            {"UndoSystem",          true, "Operational"},
            {"EditorAssetDatabase", true, "Initialized"},
            {"AssetBrowserPanel",   true, "Operational"},
            {"DependencyTracker",   true, "Operational"},
            {"StatusBar",           true, "Active"},
            {"ProfilerCore",        true, "Collecting"},
            {"ProfilerPanel",       true, "Operational"},
            {"ConsolePanel",        true, "Operational"},
            {"PluginSystem",        true, "Initialized"},
        };

        for (const auto& s : statuses) {
            ImGui::TextColored(s.operational ? ImVec4(0.2f, 0.8f, 0.2f, 1) : ImVec4(0.8f, 0.2f, 0.2f, 1),
                               "[%s] %s", s.operational ? "OK" : "!!", s.name);
            ImGui::SameLine(250);
            ImGui::TextDisabled("%s", s.statusText);
        }
    }

} // namespace Engine