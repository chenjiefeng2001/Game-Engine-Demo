#include "_3DTestApp.h"
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/RenderResources/VertexBuffer.h>
#include <Engine/Core/RenderResources/VertexArray.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/UIManager.h>
#include <Engine/ConsoleCommandRegistry.h>
#include <Engine/MemoryTracker.h>
#include <Engine/Core/RenderResources/Texture.h>

#include <glm/glm.hpp>
#include <iostream>
#include <cmath>
#include <chrono>
#include <sstream>

namespace Engine {

    constexpr int WINDOW_WIDTH  = 1280;
    constexpr int WINDOW_HEIGHT = 720;

    _3DTestApp::_3DTestApp(IGraphicsFactory& factory)
        : m_Factory(factory)
        , m_TextureManager(factory)
        , m_SceneRenderer(factory, m_TextureManager)
        , m_Camera(45.0f, (float)WINDOW_WIDTH / WINDOW_HEIGHT, 0.1f, 100.0f)
        , m_Scene("3DTest")
    {
        m_Window = m_Factory.CreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "3D Test - Coordinate Spaces");
        m_InputManager.Init(m_Window.get());
        m_SceneRenderer.SetRenderContext(*m_Window->GetContext());

        // Load 3D shader
        m_3DShader = m_Factory.CreateShader("assets/shaders/3d_lit.vert", "assets/shaders/3d_lit.frag");
        m_SceneRenderer.SetShader(m_3DShader);

        // Create MeshRenderer
        auto* ctx = m_Window->GetContext();
        m_MeshRenderer = std::make_shared<MeshRenderer>(m_Factory, *ctx);
        m_MeshRenderer->SetShader(m_3DShader);
        m_MeshRenderer->SetCamera(&m_Camera);

        // Build coordinate axes and grid GPU resources
        {
            auto axes = MeshRenderer::GenerateAxes(8.0f);
            m_AxesIndexCount = (uint32)axes.indices.size();
            m_AxesVBO = m_Factory.CreateVertexBuffer(
                reinterpret_cast<float*>(axes.vertices.data()),
                (uint32)(axes.vertices.size() * sizeof(MeshRenderer::AxisVert)));
            m_AxesVAO = m_Factory.CreateVertexArray();
            VertexAttribute axesAttrs[2] = {
                {0, 3, sizeof(MeshRenderer::AxisVert), offsetof(MeshRenderer::AxisVert, position)},
                {1, 3, sizeof(MeshRenderer::AxisVert), offsetof(MeshRenderer::AxisVert, color)},
            };
            m_AxesVAO->AddVertexBuffer(m_AxesVBO, axesAttrs, 2);
        }
        {
            auto grid = MeshRenderer::GenerateGrid(10.0f, 10);
            m_GridIndexCount = (uint32)grid.indices.size();
            m_GridVBO = m_Factory.CreateVertexBuffer(
                reinterpret_cast<float*>(grid.vertices.data()),
                (uint32)(grid.vertices.size() * sizeof(MeshRenderer::AxisVert)));
            m_GridVAO = m_Factory.CreateVertexArray();
            VertexAttribute gridAttrs[2] = {
                {0, 3, sizeof(MeshRenderer::AxisVert), offsetof(MeshRenderer::AxisVert, position)},
                {1, 3, sizeof(MeshRenderer::AxisVert), offsetof(MeshRenderer::AxisVert, color)},
            };
            m_GridVAO->AddVertexBuffer(m_GridVBO, gridAttrs, 2);
        }

        // Init UI
        InitUI();

        // Build scene
        BuildScene();

        // ── 构建 PVS ──
        {
            std::vector<GameObject*> allObjects;
            for (auto& obj : m_Scene.GetObjects())
                allObjects.push_back(obj.get());

            m_PVS.Build(allObjects, m_PVSBBoxMin, m_PVSBBoxMax, m_PVSCellSize);
            m_MeshRenderer->SetPVS(&m_PVS);
            std::cout << "[3DTest] PVS built: " << m_PVS.GetCellCount() << " cells\n";
        }

        // 默认打开性能与内存面板
        m_PerfWindow.SetVisible(true);
        m_MemoryPanel.SetVisible(true);

        std::cout << "[3DTest] Initialized.\n";
    }

    bool _3DTestApp::InitUI() {
        auto ui = m_Factory.CreateUIManager();
        if (!ui) return false;
        m_UIInitialized = UIManager::Init(std::move(ui), m_Window->GetNativeHandle(), m_Window->GetContext());
        return m_UIInitialized;
    }

    void _3DTestApp::BuildScene() {
        // Ground plane
        auto ground = std::make_shared<GameObject>("Ground");
        ground->GetTransform().SetPosition(0, -1.5f, 0);
        auto* groundMesh = ground->AddComponent<MeshComponent>();
        groundMesh->SetMesh(std::make_shared<Mesh>(Mesh::CreatePlane(10.0f, 10.0f)));
        groundMesh->m_Color = Vec4(0.3f, 0.5f, 0.3f, 1.0f);
        m_Scene.AddObject(ground);

        // Center cube (rotating)
        auto cube = std::make_shared<GameObject>("Cube");
        cube->GetTransform().SetPosition(0, 0, 0);
        auto* cubeMesh = cube->AddComponent<MeshComponent>();
        cubeMesh->SetMesh(std::make_shared<Mesh>(Mesh::CreateCube(1.0f)));
        cubeMesh->m_Color = Vec4(1.0f, 0.2f, 0.2f, 1.0f);
        m_Scene.AddObject(cube);

        // Sphere (blue)
        auto sphere = std::make_shared<GameObject>("Sphere");
        sphere->GetTransform().SetPosition(2.5f, 0, 0);
        auto* sphereMesh = sphere->AddComponent<MeshComponent>();
        sphereMesh->SetMesh(std::make_shared<Mesh>(Mesh::CreateSphere(0.8f, 24)));
        sphereMesh->m_Color = Vec4(0.2f, 0.4f, 1.0f, 1.0f);
        m_Scene.AddObject(sphere);

        // Cylinder (yellow)
        auto cylinder = std::make_shared<GameObject>("Cylinder");
        cylinder->GetTransform().SetPosition(-2.5f, 0, 0);
        auto* cylMesh = cylinder->AddComponent<MeshComponent>();
        cylMesh->SetMesh(std::make_shared<Mesh>(Mesh::CreateCylinder(0.6f, 1.5f, 24)));
        cylMesh->m_Color = Vec4(1.0f, 0.8f, 0.2f, 1.0f);
        m_Scene.AddObject(cylinder);

        // Child cube (orange, parented to Cube)
        auto childCube = std::make_shared<GameObject>("ChildCube");
        childCube->GetTransform().SetPosition(0, 1.2f, 0);
        auto* childMesh = childCube->AddComponent<MeshComponent>();
        childMesh->SetMesh(std::make_shared<Mesh>(Mesh::CreateCube(0.5f)));
        childMesh->m_Color = Vec4(1.0f, 0.6f, 0.0f, 1.0f);
        cube->AddChild(childCube);

        std::cout << "[3DTest] Scene built.\n";
    }

    // ── 渲染坐标轴（GL_LINES）──
    // 注意: 当前 IRenderContext 只支持 DrawIndexed(GL_TRIANGLES)，
    // 坐标轴/网格的 GL_LINES 绘制需要后续扩展 IRenderContext。
    // 目前通过透明的辅助网格物体替代可视化。
    void _3DTestApp::RenderCoordinateAxes() {
        // TODO: 扩展 IRenderContext 支持 DrawLines
    }

    // ── 渲染网格（GL_LINES）──
    void _3DTestApp::RenderGrid() {
        // TODO: 扩展 IRenderContext 支持 DrawLines
    }

    // ── ImGui 调试面板 ──
    void _3DTestApp::DrawDebugImGui() {
        if (!m_UIInitialized) return;

        ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin("Coordinate Spaces Debug", nullptr,
                     ImGuiWindowFlags_NoSavedSettings);

        // ════════════════════════════════════════════
        // 相机坐标信息
        // ════════════════════════════════════════════
        if (ImGui::CollapsingHeader("Camera (View Space)", ImGuiTreeNodeFlags_DefaultOpen)) {
            Vec3 pos = m_Camera.GetPosition();
            ImGui::Text("Position (world):  X=%.2f  Y=%.2f  Z=%.2f", pos.x, pos.y, pos.z);

            Vec3 fwd = m_Camera.GetForward();
            Vec3 right = m_Camera.GetRight();
            Vec3 up = m_Camera.GetUp();
            ImGui::Text("Forward:  X=%.2f  Y=%.2f  Z=%.2f", fwd.x, fwd.y, fwd.z);
            ImGui::Text("Right:    X=%.2f  Y=%.2f  Z=%.2f", right.x, right.y, right.z);
            ImGui::Text("Up:       X=%.2f  Y=%.2f  Z=%.2f", up.x, up.y, up.z);

            ImGui::Text("Pitch: %.1f   Yaw: %.1f", m_Camera.GetPitch(), m_Camera.GetYaw());
            ImGui::Text("FOV: %.1f  Aspect: %.2f  Near: %.2f  Far: %.2f",
                        m_Camera.GetFov(), m_Camera.GetAspect(),
                        m_Camera.GetNear(), m_Camera.GetFar());
        }

        // ════════════════════════════════════════════
        // 物体坐标信息
        // ════════════════════════════════════════════
        if (ImGui::CollapsingHeader("Objects (Model/World Space)", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto& obj : m_Scene.GetObjects()) {
                if (!obj || !obj->IsActive()) continue;

                auto name = obj->GetName();
                auto& t = obj->GetTransform();

                Vec3 localPos  = t.GetPosition();
                Vec3 worldPos  = t.LocalToWorld(Vec3(0,0,0)); // 原点在局部空间中变换到世界空间
                Vec3 localRot  = t.GetRotation();
                Vec3 localScale = t.GetScale();

                std::string label = name + "##" + std::to_string(reinterpret_cast<uint64>(obj.get()));
                if (ImGui::TreeNode(label.c_str())) {
                    ImGui::Text("Local Pos:   X=%.2f  Y=%.2f  Z=%.2f",
                                localPos.x, localPos.y, localPos.z);
                    ImGui::Text("World Pos:   X=%.2f  Y=%.2f  Z=%.2f",
                                worldPos.x, worldPos.y, worldPos.z);
                    ImGui::Text("Rotation:    Pitch=%.1f  Yaw=%.1f  Roll=%.1f",
                                localRot.x, localRot.y, localRot.z);
                    ImGui::Text("Scale:       X=%.2f  Y=%.2f  Z=%.2f",
                                localScale.x, localScale.y, localScale.z);

                    // 局部→世界 坐标变换
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
                                       "Coordinate Transform Examples:");
                    Vec3 origin(0,0,0);
                    Vec3 unitX(1,0,0), unitY(0,1,0), unitZ(0,0,1);
                    Vec3 wOrigin = t.LocalToWorld(origin);
                    Vec3 wX = t.LocalToWorld(unitX);
                    Vec3 wY = t.LocalToWorld(unitY);
                    Vec3 wZ = t.LocalToWorld(unitZ);
                    ImGui::Text("  Local(0,0,0) -> World(%.2f, %.2f, %.2f)",
                                wOrigin.x, wOrigin.y, wOrigin.z);
                    ImGui::Text("  Local(1,0,0) -> World(%.2f, %.2f, %.2f)",
                                wX.x, wX.y, wX.z);
                    ImGui::Text("  Local(0,1,0) -> World(%.2f, %.2f, %.2f)",
                                wY.x, wY.y, wY.z);
                    ImGui::Text("  Local(0,0,1) -> World(%.2f, %.2f, %.2f)",
                                wZ.x, wZ.y, wZ.z);

                    // 世界→观察 坐标变换
                    Vec3 viewPos = m_Camera.WorldToView(wOrigin);
                    Vec4 clipPos = m_Camera.WorldToClip(wOrigin);
                    ImGui::Text("  World -> View(%.2f, %.2f, %.2f)",
                                viewPos.x, viewPos.y, viewPos.z);
                    ImGui::Text("  World -> Clip(%.2f, %.2f, %.2f, %.2f)",
                                clipPos.x, clipPos.y, clipPos.z, clipPos.w);

                    ImGui::TreePop();
                }
            }
        }

        // ════════════════════════════════════════════
        // 渲染开关
        // ════════════════════════════════════════════
        if (ImGui::CollapsingHeader("Render Options", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show Coordinate Axes", &m_ShowAxes);
            ImGui::SameLine();
            ImGui::Checkbox("Show Grid", &m_ShowGrid);

            ImGui::Separator();
            float ambient[3] = {m_MeshRenderer->GetAmbientColor().x,
                                m_MeshRenderer->GetAmbientColor().y,
                                m_MeshRenderer->GetAmbientColor().z};
            if (ImGui::ColorEdit3("Ambient Color", ambient)) {
                m_MeshRenderer->SetAmbientColor(Vec3(ambient[0], ambient[1], ambient[2]));
            }

            Vec3 lightPos = m_MeshRenderer->GetLightPosition();
            if (ImGui::DragFloat3("Light Position", &lightPos.x, 0.1f)) {
                m_MeshRenderer->SetLightPosition(lightPos);
            }
        }

        // ════════════════════════════════════════════
        // 潜在可见集 (PVS)
        // ════════════════════════════════════════════
        if (ImGui::CollapsingHeader("PVS (Visibility Culling)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable PVS Culling", &m_PVSEnabled)) {
                m_PVS.SetConfig(PVSConfig{ m_PVSCellSize, 0.5f, m_PVSEnabled, m_PVSDebugDraw });
            }

            ImGui::Checkbox("Debug Draw Cells", &m_PVSDebugDraw);
            m_PVS.SetConfig(PVSConfig{ m_PVSCellSize, 0.5f, m_PVSEnabled, m_PVSDebugDraw });

            bool rebuild = false;
            rebuild |= ImGui::DragFloat3("BBox Min", &m_PVSBBoxMin.x, 0.5f);
            rebuild |= ImGui::DragFloat3("BBox Max", &m_PVSBBoxMax.x, 0.5f);
            rebuild |= ImGui::DragFloat3("Cell Size", &m_PVSCellSize.x, 0.5f);
            if (m_PVSCellSize.x < 1.0f) m_PVSCellSize.x = 1.0f;
            if (m_PVSCellSize.y < 1.0f) m_PVSCellSize.y = 1.0f;
            if (m_PVSCellSize.z < 1.0f) m_PVSCellSize.z = 1.0f;

            if (rebuild) {
                std::vector<GameObject*> allObjects;
                for (auto& obj : m_Scene.GetObjects())
                    allObjects.push_back(obj.get());
                m_PVS.Build(allObjects, m_PVSBBoxMin, m_PVSBBoxMax, m_PVSCellSize);
            }

            if (m_PVS.IsValid()) {
                ImGui::Separator();
                Vec3 camPos = m_Camera.GetPosition();
                uint32 cellIdx = m_PVS.GetCellIndex(camPos);
                uint32 total = m_PVS.GetCellCount();
                uint32 visible = m_PVS.GetVisibleCellCount(camPos);
                ImGui::Text("Camera Cell: %u / %u", cellIdx, total);
                ImGui::Text("Visible Cells: %u (%.1f%%)", visible,
                             total > 0 ? (float)visible / total * 100.0f : 0.0f);
            }
        }

        // ════════════════════════════════════════════
        // 抗锯齿设置
        // ════════════════════════════════════════════
        if (ImGui::CollapsingHeader("Anti-Aliasing", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* aaModes[] = { "None", "MSAA (Multisample)", "SSAA (Supersample)",
                                      "CSAA (Coverage)", "MLAA (Morphological)" };
            int prevMode = m_CurrentAAMode;
            int prevSampleIndex = m_AASampleIndex;
            float prevScale = m_AASCale;

            ImGui::Combo("Mode", &m_CurrentAAMode, aaModes, IM_ARRAYSIZE(aaModes));

            // 样本数选择（仅 MSAA / CSAA / SSAA）
            if (m_CurrentAAMode >= 1 && m_CurrentAAMode <= 3) {
                const char* sampleItems[] = { "2x", "4x", "8x" };
                ImGui::Combo("Sample Count", &m_AASampleIndex, sampleItems, 3);
                ImGui::Text("Active: %dx", k_SampleValues[m_AASampleIndex]);
            }

            // SSAA 缩放比例
            if (m_CurrentAAMode == 2) {
                ImGui::SliderFloat("SSAA Scale", &m_AASCale, 1.0f, 4.0f, "%.1fx");
            }

            // 检测变更：模式切换 / 样本数变化 / 缩放变化
            bool changed = (prevMode != m_CurrentAAMode);
            if (m_CurrentAAMode >= 1 && m_CurrentAAMode <= 3) {
                changed = changed || (prevSampleIndex != m_AASampleIndex);
            }
            if (m_CurrentAAMode == 2) {
                changed = changed || (std::abs(prevScale - m_AASCale) > 0.01f);
            }

            if (changed) {
                int sampleCount = (m_CurrentAAMode >= 1 && m_CurrentAAMode <= 3)
                                  ? k_SampleValues[m_AASampleIndex] : 4;

                m_AADemoConfig.sampleCount = sampleCount;
                m_AADemoConfig.superSamplingScale = m_AASCale;

                switch (m_CurrentAAMode) {
                    case 0: m_AADemoConfig.mode = AntiAliasingMode::None; break;
                    case 1: m_AADemoConfig.mode = AntiAliasingMode::MSAA; break;
                    case 2: m_AADemoConfig.mode = AntiAliasingMode::SSAA; break;
                    case 3: m_AADemoConfig.mode = AntiAliasingMode::CSAA; break;
                    case 4: m_AADemoConfig.mode = AntiAliasingMode::MLAA; break;
                }

                m_Factory.SetAntiAliasingConfig(m_AADemoConfig);
            }

            // 显示当前 AA 状态
            ImGui::Separator();
            ImGui::Text("Current: %s",
                         m_AADemoConfig.mode == AntiAliasingMode::None ? "Disabled" : "Enabled");
            ImGui::Text("Samples: %dx", m_AADemoConfig.sampleCount);
        }

        // ════════════════════════════════════════════
        // 调试工具开关
        // ════════════════════════════════════════════
        if (ImGui::CollapsingHeader("Debug Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool perfVis = m_PerfWindow.IsVisible();
            if (ImGui::Checkbox("Performance Window", &perfVis)) {
                if (perfVis) m_PerfWindow.SetVisible(true);
                else m_PerfWindow.SetVisible(false);
            }

            bool memVis = m_MemoryPanel.IsVisible();
            if (ImGui::Checkbox("Memory Panel", &memVis)) {
                if (memVis) m_MemoryPanel.SetVisible(true);
                else m_MemoryPanel.SetVisible(false);
            }

            ImGui::Text("Toggle Console:  ~  (Grave Accent)");
        }

        ImGui::End();
    }

    void _3DTestApp::HandleInput(float dt) {
        if (Input::IsKeyPressed(KeyCode::GraveAccent)) {
            m_ConsolePanel.ToggleVisibility();
        }
        bool consoleCaptures = m_ConsolePanel.IsCapturingInput();
        Input::SetBlockInput(consoleCaptures, consoleCaptures);
        if (consoleCaptures) return;

        Vec3 pos = m_Camera.GetPosition();
        float speed = m_CameraSpeed * dt;
        Vec3 fwd = m_Camera.GetForward();
        Vec3 right = m_Camera.GetRight();

        if (Input::IsKeyDown(KeyCode::W)) {
            pos.x += fwd.x * speed; pos.y += fwd.y * speed; pos.z += fwd.z * speed;
        }
        if (Input::IsKeyDown(KeyCode::S)) {
            pos.x -= fwd.x * speed; pos.y -= fwd.y * speed; pos.z -= fwd.z * speed;
        }
        if (Input::IsKeyDown(KeyCode::A)) {
            pos.x -= right.x * speed; pos.y -= right.y * speed; pos.z -= right.z * speed;
        }
        if (Input::IsKeyDown(KeyCode::D)) {
            pos.x += right.x * speed; pos.y += right.y * speed; pos.z += right.z * speed;
        }
        if (Input::IsKeyDown(KeyCode::Q)) pos.y -= speed;
        if (Input::IsKeyDown(KeyCode::E)) pos.y += speed;

        m_Camera.SetPosition(pos);

        float yaw   = m_Camera.GetYaw();
        float pitch = m_Camera.GetPitch();
        if (Input::IsKeyDown(KeyCode::Left))  yaw   -= 50.0f * dt;
        if (Input::IsKeyDown(KeyCode::Right)) yaw   += 50.0f * dt;
        if (Input::IsKeyDown(KeyCode::Up))    pitch += 50.0f * dt;
        if (Input::IsKeyDown(KeyCode::Down))  pitch -= 50.0f * dt;
        if (pitch > 89.0f)  pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
        m_Camera.SetRotation(pitch, yaw);
    }

    void _3DTestApp::Run() {
        using namespace std::chrono;
        auto last = high_resolution_clock::now();
        float lightAngle = 0.0f;

        while (!m_Window->ShouldClose()) {
            auto now = high_resolution_clock::now();
            float dt = duration<float>(now - last).count();
            last = now;
            if (dt > 0.05f) dt = 0.05f;

            m_Window->PollEvents();
            HandleInput(dt);

            MemoryTracker::FrameStart();

            // ── Update scene (rotate cube) ──
            static float rotAngle = 0.0f;
            rotAngle += dt * 30.0f;
            auto* cubeObj = m_Scene.FindObject("Cube");
            if (cubeObj) {
                cubeObj->GetTransform().SetRotation(rotAngle * 0.5f, rotAngle, 0);
            }

            // ── Rotating light ──
            lightAngle += dt * 20.0f;
            float rad = lightAngle * 3.14159265f / 180.0f;
            Vec3 lightPos(5.0f * std::cos(rad), 5.0f, 5.0f * std::sin(rad));
            m_MeshRenderer->SetLightPosition(lightPos);

            // ── Render 3D ──
            auto* ctx = m_Window->GetContext();
            if (ctx) {
                ctx->ClearColor(0.08f, 0.08f, 0.12f, 1.0f);

                // Bind the 3D shader for coordinate axes & grid (they use the same shader)
                m_3DShader->Bind();
                m_3DShader->SetMat4("u_View",       m_Camera.GetViewMatrix().Data());
                m_3DShader->SetMat4("u_Projection", m_Camera.GetProjectionMatrix().Data());
                Vec3 ambient = m_MeshRenderer->GetAmbientColor();
                m_3DShader->SetVec3("u_AmbientColor", &ambient.x);
                Vec3 lightCol(1,1,1);
                m_3DShader->SetVec3("u_LightColor",   &lightCol.x);
                m_3DShader->SetFloat("u_LightIntensity", 1.0f);
                Vec3 viewPos = m_Camera.GetPosition();
                m_3DShader->SetVec3("u_ViewPos", &viewPos.x);
                m_3DShader->SetVec3("u_LightPos",  &lightPos.x);
                m_3DShader->SetInt("u_HasTexture", 0);

                // ── Draw grid (GL_LINES via simple triangle trick: we use thin quads) ──
                // For now we skip lines rendering and just show the 3D objects.

                // ── Draw 3D meshes (PVS 加速) ──
                std::vector<GameObject*> allObjects;
                for (auto& obj : m_Scene.GetObjects()) {
                    allObjects.push_back(obj.get());
                }
                Vec3 camPos = m_Camera.GetPosition();
                m_MeshRenderer->RenderWithPVS(allObjects, camPos);

                // ── Draw coordinate axes using a simple shader approach ──
                // (skip GL_LINES for now until IRenderContext is extended)
            }

            // ── 性能统计（在 UI 构建前收集 DrawCall） ──
            uint32 drawCalls = ctx ? ctx->GetAndResetDrawCallCount() : 0;
            m_PerfWindow.FeedStats(dt * 1000.0f, drawCalls, 0, 0, 0, 0);

            // ── Debug ImGui ──
            if (m_UIInitialized && UIManager::Get()) {
                UIManager::Begin();
                m_PerfWindow.OnImGui();
                DrawDebugImGui();
                m_ConsolePanel.OnImGui();
                m_MemoryPanel.OnImGui();
                UIManager::End();

                // Block input to game when ImGui is capturing
                ImGuiIO& io = ImGui::GetIO();
                Input::SetBlockInput(io.WantCaptureMouse, io.WantCaptureKeyboard);
            }

            MemoryTracker::FrameEnd();

            m_InputManager.OnUpdate();
            if (ctx) ctx->SwapBuffers();
        }

        // Cleanup
        if (m_UIInitialized) {
            UIManager::Shutdown();
            m_UIInitialized = false;
        }
        std::cout << "[3DTest] Shutdown complete.\n";
    }

} // namespace Engine
