#include "_3DTestApp.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Profiler.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/MemoryTracker.h"
#include <GLFW/glfw3.h>
#include <chrono>

namespace Engine {

    _3DTestApp::_3DTestApp(IGraphicsFactory& factory)
        : m_Factory(factory)
        , m_TextureManager(factory)
        , m_Camera(60.0f, 1280.0f / 720.0f, 0.1f, 200.0f)
    {
        // Note: Log::Init is called in Run() to avoid Tracy ETW stack overflow
        // during static initialization phase.

        m_Window = m_Factory.CreateWindow(1280, 720, "3D Debug Test");
        m_InputManager.Init(m_Window.get());

        m_Camera.SetPosition(Vec3(0, 10, 25));
        m_Camera.SetRotation(-15, 0);

        auto* ctx = m_Window->GetContext();
        m_MeshRenderer = std::make_unique<MeshRenderer>(m_Factory, *ctx);
        m_MeshRenderer->SetCamera(&m_Camera);
        m_MeshRenderer->SetAmbientColor(Vec3(0.2f, 0.2f, 0.25f));

        // 3D Shader
        m_3DShader = m_Factory.CreateShader("assets/shaders/3d_lit.vert", "assets/shaders/3d_lit.frag");
        m_MeshRenderer->SetShader(m_3DShader);

        // Depth shader
        m_DepthShader = m_Factory.CreateShader("assets/shaders/depth_only.vert", "assets/shaders/depth_only.frag");
        m_MeshRenderer->SetDepthShader(m_DepthShader);

        // Shadow mapper
        m_ShadowMapper = std::make_unique<ShadowMapper>(*ctx);
        m_MeshRenderer->SetShadowMapper(m_ShadowMapper.get());
        m_MeshRenderer->SetShadowEnabled(true);

        // Lights
        m_MeshRenderer->AddLight({ {15, 20, 15}, {1, 1, 1}, 1.5f });
        m_MeshRenderer->AddLight({ {-10, 8, 12}, {0.8f, 0.6f, 1.0f}, 0.8f });
        m_MeshRenderer->AddLight({ {5, 3, -12}, {1.0f, 0.4f, 0.2f}, 0.6f });

        // Console panel
        m_ConsolePanel.SetVisible(false);

        // ✨ 重要：注册控制台面板到 Application 静态指针
        // 使 ~ 键可在全局切换控制台
        // 这里我们手动处理，不依赖 Application::SetConsolePanel

        Log::Info("[3DTest] Initialized. Controls: WASD=move, Q/E=up/down, RMB+drag=look, ~=console");
    }

    _3DTestApp::~_3DTestApp() {
        if (m_UIInitialized) {
            UIManager::Shutdown();
            m_UIInitialized = false;
        }
        Log::Shutdown();
    }

    bool _3DTestApp::InitUI() {
        auto ui = m_Factory.CreateUIManager();
        if (!ui) return false;
        m_UIInitialized = UIManager::Init(std::move(ui),
                                           m_Window->GetNativeHandle(),
                                           m_Window->GetContext());
        return m_UIInitialized;
    }

    // ============================================================
    // HandleInput — WASD + 鼠标 + 菜单/控制台切换
    // ============================================================

    void _3DTestApp::HandleInput(float dt) {
        // ── 控制台切换（~ 键） ──
        if (Input::IsKeyPressed(KeyCode::GraveAccent)) {
            m_ConsolePanel.ToggleVisibility();
        }

        // 控制台捕获输入时阻止游戏操作
        bool consoleCaptures = m_ConsolePanel.IsCapturingInput();
        Input::SetBlockInput(consoleCaptures, consoleCaptures);
        if (consoleCaptures) return;

        // ── 菜单导航 ──
        if (m_MenuManager.isVisible) {
            if (Input::IsKeyPressed(KeyCode::Up))       m_MenuManager.OnKeyPressed(KeyCode::Up);
            if (Input::IsKeyPressed(KeyCode::Down))     m_MenuManager.OnKeyPressed(KeyCode::Down);
            if (Input::IsKeyPressed(KeyCode::Left))     m_MenuManager.OnKeyPressed(KeyCode::Left);
            if (Input::IsKeyPressed(KeyCode::Right))    m_MenuManager.OnKeyPressed(KeyCode::Right);
            if (Input::IsKeyPressed(KeyCode::Escape))   m_MenuManager.OnKeyPressed(KeyCode::Escape);
            if (Input::IsKeyPressed(KeyCode::Enter))    m_MenuManager.OnKeyPressed(KeyCode::Enter);
            return; // 菜单打开时阻止游戏输入
        }

        // ── ESC → 打开菜单 ──
        if (Input::IsKeyPressed(KeyCode::Escape)) {
            m_MenuManager.isVisible = !m_MenuManager.isVisible;
            if (m_MenuManager.isVisible && m_MenuManager.currentPage == MenuManager::Page::None)
                m_MenuManager.currentPage = MenuManager::Page::MainMenu;
            return;
        }

        // ── Helper Toggles 快捷键 ──
        auto* ctx = m_Window->GetContext();
        if (ctx) {
            auto* ht = ctx->GetHelperTogglesData();
            if (ht) {
                if (Input::IsKeyPressed(KeyCode::F1)) ht->showGrid = !ht->showGrid;
                if (Input::IsKeyPressed(KeyCode::F2)) ht->showColliders = !ht->showColliders;
                if (Input::IsKeyPressed(KeyCode::F3)) ht->showBones = !ht->showBones;
                if (Input::IsKeyPressed(KeyCode::F10)) {
                    ht->pauseRendering = !ht->pauseRendering;
                    if (ht->pauseRendering) ht->stepOneFrame = false;
                }
                if (Input::IsKeyPressed(KeyCode::F11)) {
                    ht->stepOneFrame = true;
                    ht->pauseRendering = false;
                }
            }
        }

        // ── 摄像机移动（WASD + Q/E） ──
        Vec3 pos = m_Camera.GetPosition();
        float speed = m_CameraSpeed * dt;
        Vec3 fwd = m_Camera.GetForward();
        Vec3 right = m_Camera.GetRight();

        if (Input::IsKeyDown(KeyCode::W)) { pos.x += fwd.x * speed; pos.y += fwd.y * speed; pos.z += fwd.z * speed; }
        if (Input::IsKeyDown(KeyCode::S)) { pos.x -= fwd.x * speed; pos.y -= fwd.y * speed; pos.z -= fwd.z * speed; }
        if (Input::IsKeyDown(KeyCode::A)) { pos.x -= right.x * speed; pos.y -= right.y * speed; pos.z -= right.z * speed; }
        if (Input::IsKeyDown(KeyCode::D)) { pos.x += right.x * speed; pos.y += right.y * speed; pos.z += right.z * speed; }
        if (Input::IsKeyDown(KeyCode::Q)) pos.y -= speed;
        if (Input::IsKeyDown(KeyCode::E)) pos.y += speed;
        m_Camera.SetPosition(pos);

        // ── 自由视角旋转（右键拖拽） ──
        float yaw = m_Camera.GetYaw();
        float pitch = m_Camera.GetPitch();
        if (Input::IsMouseButtonDown(MouseCode::ButtonRight)) {
            yaw   += Input::GetMouseDeltaX() * m_MouseSensitivity;
            pitch += Input::GetMouseDeltaY() * m_MouseSensitivity * 0.8f;
        }
        pitch = std::max(-89.0f, std::min(89.0f, pitch));
        m_Camera.SetRotation(pitch, yaw);
    }

    // ============================================================
    // DrawDebugImGui — 自定义调试面板
    // ============================================================

    void _3DTestApp::DrawDebugImGui() {
        if (!m_UIInitialized) return;

        ImGui::SetNextWindowPos(ImVec2(10, 410), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 120), ImGuiCond_FirstUseEver);

        ImGui::Begin("3D Test Info", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1), "3D Debug Test App");
        ImGui::Separator();

        Vec3 camPos = m_Camera.GetPosition();
        ImGui::Text("Camera: (%.1f, %.1f, %.1f)", camPos.x, camPos.y, camPos.z);
        ImGui::Text("Frame: %u", m_FrameCount);
        ImGui::Text("Objects: %zu", m_SceneObjects.size());
        ImGui::Text("F1: Grid  F2: Colliders  F3: Bones");
        ImGui::Text("F10: Pause  F11: Step  ESC: Menu");

        auto* ctx = m_Window->GetContext();
        if (ctx) {
            ViewMode vm = ctx->GetViewMode();
            ImGui::Text("View Mode: %s", ViewModeToString(vm));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1),
                           "Open 'Graphics Debug Tools' window\n"
                           "to access all 12 debug panels.");

        ImGui::End();
    }

    // ============================================================
    // Run — 主循环
    // ============================================================

    void _3DTestApp::Run() {
        using namespace std::chrono;
        auto last = high_resolution_clock::now();

        // 初始化 UI
        InitUI();

        // 设置菜单回调
        m_MenuManager.SetStartGameCallback([this]() {
            // "New Game": 关闭菜单，开始游戏
            m_MenuManager.isVisible = false;
            m_MenuManager.currentPage = MenuManager::Page::None;
        });
        m_MenuManager.SetQuitCallback([this]() {
            // "Quit": 关闭窗口
            if (m_Window) {
                glfwSetWindowShouldClose(
                    static_cast<GLFWwindow*>(m_Window->GetNativeHandle()), GLFW_TRUE);
            }
        });

        // 启动时有菜单
        m_MenuManager.isVisible = true;
        m_MenuManager.currentPage = MenuManager::Page::MainMenu;
        m_PerfWindow.SetVisible(true);

        while (!m_Window->ShouldClose()) {
            auto now = high_resolution_clock::now();
            float dt = duration<float>(now - last).count();
            last = now;
            if (dt > 0.05f) dt = 0.05f;

            m_Window->PollEvents();
            HandleInput(dt);
            MemoryTracker::FrameStart();

            // ── 检测窗口 resize ──
            {
                int w, h;
                glfwGetFramebufferSize(
                    static_cast<GLFWwindow*>(m_Window->GetNativeHandle()), &w, &h);
                static int lastW = 0, lastH = 0;
                if (w != lastW || h != lastH) {
                    lastW = w; lastH = h;
                    if (w > 0 && h > 0) OnWindowResize(w, h);
                }
            }

            ++m_FrameCount;

            // ── 更新场景 ──
            m_MenuManager.OnUpdate(dt);

            // 动画光源
            if (m_AnimateLights) {
                m_LightAngle += dt * 15.0f;
                float rad = m_LightAngle * 3.14159f / 180.0f;
                Vec3 lightPos(std::cos(rad) * 25.0f, 20.0f, std::sin(rad) * 25.0f);
                m_MeshRenderer->SetLightPosition(lightPos);
            }

            // ── 渲染 ──
            auto* ctx = m_Window->GetContext();

            // 暂停/步进控制
            bool shouldRender = true;
            if (ctx) {
                auto* ht = ctx->GetHelperTogglesData();
                if (ht) {
                    if (ht->pauseRendering) shouldRender = false;
                    if (ht->stepOneFrame) {
                        shouldRender = true;
                        ht->stepOneFrame = false;
                        ht->pauseRendering = true;
                    }
                }
            }

            if (shouldRender && ctx) {
                ctx->ClearColor(0.1f, 0.1f, 0.15f, 1.0f);

                // ── GPU Pass 标记 ──
                // Shadow Pass
                {
                    int32 q = ctx->BeginGPUPass("ShadowPass");
                    if (m_ShadowMapper) {
                        m_ShadowMapper->BindForShadowPass();
                        m_ShadowMapper->EndShadowPass();
                    }
                    ctx->EndGPUPass(q);
                }

                // Base Pass
                {
                    int32 q = ctx->BeginGPUPass("BasePass");

                    // 绑定 View Mode uniform
                    if (m_3DShader) {
                        m_3DShader->Bind();
                        ctx->BindViewModeUniform(m_3DShader.get());
                    }

                    // 设置光源 uniform
                    m_MeshRenderer->RenderWithSceneGraph(m_SceneObjects);

                    // 辅助可视化
                    RenderHelperVisualizations();

                    ctx->EndGPUPass(q);
                }

                // PostProcess Pass
                {
                    int32 q = ctx->BeginGPUPass("PostProcess");
                    // 后期处理占位
                    ctx->EndGPUPass(q);
                }

                // Resolve AA
                static_cast<OpenGLContext*>(ctx)->ResolveToDefault();
            }

            // ── 填充调试数据 ──
            if (ctx) {
                PopulateLightingDebugData();
                PopulateGeometryDebugData();
                PopulatePostProcessingDebugData();
                PopulateTextureDebugData();
            }

            // ── 统计数据 ──
            uint32 drawCalls = ctx ? ctx->GetAndResetDrawCallCount() : 0;
            uint32 vertexCount = ctx ? ctx->GetAndResetVertexCount() : 0;
            uint32 triCount = ctx ? ctx->GetAndResetTriangleCount() : 0;

            m_PerfWindow.FeedStats(dt * 1000.0f, drawCalls,
                                    (uint32)m_SceneObjects.size(), 0, 0, 0);
            m_PerfWindow.SetGeometryCount(vertexCount, triCount);
            m_PerfWindow.SetCPUTime(dt * 1000.0f);

            // GPU Profiler
            if (ctx) {
                GPUProfileFrame gpuFrame;
                if (ctx->GetGPUProfileFrame(gpuFrame)) {
                    m_PerfWindow.SetGPUTime(gpuFrame.GetTotalMs());
                    GPUProfilerSnapshot snap;
                    snap.passCount = std::min(gpuFrame.passCount,
                                              (uint32)GPUProfilerSnapshot::kMaxPasses);
                    for (uint32 i = 0; i < snap.passCount; ++i) {
                        snap.passes[i].name   = gpuFrame.passes[i].passName;
                        snap.passes[i].timeMs = gpuFrame.passes[i].elapsedMs;
                    }
                    m_PerfWindow.SetGPUProfiler(snap);
                }

                m_PerfWindow.SetVRAMUsage(ctx->GetTextureVRAMBytes(),
                                           ctx->GetBufferVRAMBytes(), 0);
            }

            // ── ImGui ──
            if (m_UIInitialized && UIManager::Get()) {
                UIManager::Begin();

                // 性能调试面板
                m_PerfWindow.OnImGui();

                // 自定义调试面板
                DrawDebugImGui();

                // 控制台
                m_ConsolePanel.OnImGui();

                // 菜单
                if (m_MenuManager.isVisible)
                    m_MenuManager.OnImGui();

                UIManager::End();

                ImGuiIO& io = ImGui::GetIO();
                Input::SetBlockInput(io.WantCaptureMouse, io.WantCaptureKeyboard);
            }

            MemoryTracker::FrameEnd();
            m_InputManager.OnUpdate();

            glfwSwapBuffers(static_cast<GLFWwindow*>(m_Window->GetNativeHandle()));
        }
    }

    // ============================================================
    // 调试数据填充
    // ============================================================

    void _3DTestApp::PopulateLightingDebugData() {
        auto* ctx = m_Window->GetContext();
        if (!ctx) return;
        auto* ld = ctx->GetLightingDebugData();
        if (!ld) return;

        ld->lightCount = (uint32)std::min(m_MeshRenderer->GetLightCount(), (size_t)LightingDebugFrameData::kMaxLights);
        for (uint32 i = 0; i < ld->lightCount; ++i) {
            auto& dst = ld->lights[i];
            char buf[32]; std::snprintf(buf, sizeof(buf), "Light_%u", i);
            dst.name = buf;
            dst.position = m_MeshRenderer->GetLight(i).position;
            dst.color = m_MeshRenderer->GetLight(i).color;
            dst.intensity = m_MeshRenderer->GetLight(i).intensity;
            dst.enabled = true;
            dst.isShadowCaster = (i == 0);
        }
        ld->ambientColor = m_MeshRenderer->GetAmbientColor();
        ld->ambientIntensity = 1.0f;
        ld->shadowMapSize = 2048;
        ld->shadowBias = 0.005f;
        ld->shadowsEnabled = m_MeshRenderer->IsShadowEnabled();

        ld->cascades.cascadeCount = 4;
        ld->cascades.cascadeSplits[0] = 0.05f;
        ld->cascades.cascadeSplits[1] = 0.15f;
        ld->cascades.cascadeSplits[2] = 0.4f;
        ld->cascades.cascadeSplits[3] = 1.0f;

        if (m_ShadowMapper) {
            ld->shadowTextureHandle = m_ShadowMapper->GetShadowTexture();
            ld->shadowTexWidth = m_ShadowMapper->GetConfig().resolution;
            ld->shadowTexHeight = m_ShadowMapper->GetConfig().resolution;
        }
    }

    void _3DTestApp::PopulateGeometryDebugData() {
        auto* ctx = m_Window->GetContext();
        if (!ctx) return;
        auto* gd = ctx->GetGeometryDebugData();
        if (!gd) return;

        gd->totalObjects = (uint32)m_SceneObjects.size() + 1;
        gd->visibleObjects = gd->totalObjects;
        gd->culledObjects = 0;

        AABB sb;
        sb.Expand(Vec3(-15, -5, -15));
        sb.Expand(Vec3(15, 15, 15));
        gd->sceneBounds = sb;
        gd->lod.currentLODCount = 3;
    }

    void _3DTestApp::PopulatePostProcessingDebugData() {
        auto* ctx = m_Window->GetContext();
        if (!ctx) return;
        auto* pp = ctx->GetPostProcessingDebugData();
        if (!pp) return;

        pp->postProcessingEnabled = true;
        pp->bloomEnabled = true;
        pp->bloomIntensity = 1.2f;
        pp->bloomThreshold = 1.0f;
        pp->colorGradingEnabled = true;
        pp->colorGradingStrength = 0.8f;
        pp->brightness = 1.0f;
        pp->contrast = 1.1f;
        pp->saturation = 1.05f;
        pp->antiAliasingEnabled = true;
        pp->aaType = AAType::FXAA;
        pp->toneMappingEnabled = true;
        pp->toneMapMode = ToneMappingMode::ACES;
        pp->exposure = 1.0f;
        pp->gamma = 2.2f;
    }

    void _3DTestApp::PopulateTextureDebugData() {
        auto* ctx = m_Window->GetContext();
        if (!ctx) return;
        auto* td = ctx->GetTextureDebugData();
        if (!td) return;

        const char* texNames[] = {
            "test.png", "brick_diffuse.dds", "metal_roughness.dds",
            "skybox_cubemap.dds", "normal_map.png", "shadow_map", "checker_placeholder"
        };
        const uint32 texW[] = {512, 1024, 512, 2048, 256, 2048, 128};
        const uint32 texH[] = {512, 1024, 512, 2048, 256, 2048, 128};
        const uint32 mips[] = {10, 11, 10, 12, 9, 1, 7};
        const uint64 vram[] = {1048576, 16777216, 4194304, 33554432, 262144, 16777216, 65536};

        td->textureCount = 7;
        td->totalVRAM = 0;
        for (uint32 i = 0; i < 7; ++i) {
            auto& tex = td->textures[i];
            tex.name = texNames[i];
            tex.width = texW[i]; tex.height = texH[i];
            tex.mipCount = mips[i];
            tex.vramBytes = vram[i];
            td->totalVRAM += vram[i];
            tex.isStreaming = (i >= 3 && i <= 5);
            tex.isLoading = (i == 4);
            tex.streamProgress = (i == 4) ? 0.65f : 1.0f;
            tex.isResident = !tex.isLoading;
        }
    }

    // ============================================================
    // 辅助可视化
    // ============================================================

    void _3DTestApp::RenderHelperVisualizations() {
        auto* ctx = m_Window->GetContext();
        if (!ctx) return;
        auto* ht = ctx->GetHelperTogglesData();
        if (!ht) return;

        if (ht->showGrid) DrawGrid(ht->gridSize, ht->gridSteps);
        if (ht->showOriginAxis) DrawOriginAxis();

        if (ht->showColliders) {
            auto* batch = m_MeshRenderer->GetBatch();
            if (batch) {
                batch->Begin(PrimitiveType::Lines);
                Vec3 corners[8] = {
                    {-2,-2,-2}, {2,-2,-2}, {2,-2,2}, {-2,-2,2},
                    {-2,2,-2}, {2,2,-2}, {2,2,2}, {-2,2,2}
                };
                int edges[24] = {0,1,1,2,2,3,3,0,4,5,5,6,6,7,7,4,0,4,1,5,2,6,3,7};
                Vec4 wc(ht->colliderColor[0], ht->colliderColor[1], ht->colliderColor[2], 1.0f);
                for (int e = 0; e < 24; e += 2)
                    batch->Line(corners[edges[e]], corners[edges[e+1]], wc);
                batch->End();
            }
        }

        if (ht->showBones) {
            auto* batch = m_MeshRenderer->GetBatch();
            if (batch) {
                batch->Begin(PrimitiveType::Lines);
                Vec4 boneCol(1.0f, 1.0f, 0.0f, 1.0f);
                batch->Line(Vec3(0,0,0), Vec3(0,5,0), boneCol);
                batch->Line(Vec3(0,5,0), Vec3(1.5f,6,0), boneCol);
                batch->Line(Vec3(0,5,0), Vec3(-1.5f,6,0), boneCol);
                batch->End();
            }
        }
    }

    void _3DTestApp::DrawGrid(float size, int steps) {
        auto* batch = m_MeshRenderer->GetBatch();
        if (!batch) return;
        batch->Begin(PrimitiveType::Lines);
        float half = size * 0.5f;
        float step = size / steps;
        Vec4 gc(0.3f, 0.3f, 0.3f, 0.6f);
        Vec4 ax(1,0.2f,0.2f,0.8f), az(0.2f,0.2f,1,0.8f);
        for (int i = 0; i <= steps; ++i) {
            float t = -half + i * step;
            batch->Line(Vec3(-half,0,t), Vec3(half,0,t), (i==steps/2)?ax:gc);
            batch->Line(Vec3(t,0,-half), Vec3(t,0,half), (i==steps/2)?az:gc);
        }
        batch->End();
    }

    void _3DTestApp::DrawOriginAxis() {
        auto* batch = m_MeshRenderer->GetBatch();
        if (!batch) return;
        batch->Begin(PrimitiveType::Lines);
        batch->Line(Vec3(0,0,0), Vec3(3,0,0), Vec4(1,0,0,1));
        batch->Line(Vec3(0,0,0), Vec3(0,3,0), Vec4(0,1,0,1));
        batch->Line(Vec3(0,0,0), Vec3(0,0,3), Vec4(0,0,1,1));
        batch->End();
    }

    void _3DTestApp::OnWindowResize(int width, int height) {
        float aspect = (float)width / (float)height;
        m_Camera.SetAspectRatio(aspect);
        if (auto* ctx = m_Window->GetContext())
            ctx->OnResize(width, height);
    }

} // namespace Engine