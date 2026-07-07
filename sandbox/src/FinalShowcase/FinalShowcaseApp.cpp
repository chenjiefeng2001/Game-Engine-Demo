/**
 * @file FinalShowcaseApp.cpp
 * @brief 最终展示应用 — 基于 RenderTestApp 模式重构
 */

#include "FinalShowcaseApp.h"
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include <Engine/Core/RHI/RHIBackend.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/UIManager.h>
#include <Engine/PerformanceWindow.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cstdio>
#include <cmath>

namespace Engine {

static GameObject* MakeSceneObj(Scene& scene, const std::string& name,
                                 std::shared_ptr<Mesh> mesh, const Vec3& pos,
                                 const Vec4& color,
                                 const Vec3& scale = Vec3(1, 1, 1)) {
    auto obj = std::make_shared<GameObject>(name);
    obj->GetTransform().SetPosition(pos);
    obj->GetTransform().SetScale(scale);
    auto* mc = obj->AddComponent<MeshComponent>();
    mc->SetMesh(std::move(mesh));
    mc->m_Color = color;
    scene.AddObject(obj);
    return obj.get();
}

// ── 构造 / 析构 ──

FinalShowcaseApp::FinalShowcaseApp(IGraphicsFactory& factory)
    : m_Factory(factory), m_PerfWindow() {}

FinalShowcaseApp::~FinalShowcaseApp() {
    UIManager::Shutdown();
}

// ── 场景构建 ──

void FinalShowcaseApp::BuildScene() {
    m_Scene = std::make_unique<Scene>("FinalShowcase");

    // 地面 (1000x1000 平面)
    auto groundMesh = std::make_shared<Mesh>(Mesh::CreatePlane(1000, 1000));
    auto ground = std::make_shared<GameObject>("Ground");
    ground->GetTransform().SetPosition(Vec3(0, -2.5f, 0));
    auto* gmc = ground->AddComponent<MeshComponent>();
    gmc->SetMesh(groundMesh);
    gmc->m_Color = Vec4(0.18f, 0.22f, 0.18f, 1.0f);
    m_Scene->AddObject(ground);
    m_SceneObjects.push_back(ground.get());

    // 中心金属球 (PBR 展示)
    auto sphereM = std::make_shared<Mesh>(Mesh::CreateSphere(1.5f, 48));
    auto* sphere = MakeSceneObj(*m_Scene, "CenterSphere", sphereM,
                                Vec3(0, 1.0f, 0), Vec4(0.9f, 0.6f, 0.2f, 1.0f), Vec3(1,1,1));

    // 金属环 (Torus)
    auto torusM = std::make_shared<Mesh>(Mesh::CreateTorus(0.8f, 1.8f, 24));
    auto* torus = MakeSceneObj(*m_Scene, "Ring", torusM,
                               Vec3(0, 1.0f, 0), Vec4(0.3f, 0.7f, 1.0f, 1.0f));

    // 周围一圈物体
    auto cubeM = std::make_shared<Mesh>(Mesh::CreateCube(1.2f));
    auto cylM = std::make_shared<Mesh>(Mesh::CreateCylinder(0.8f, 2.0f, 16));
    for (int i = 0; i < 8; ++i) {
        float a = (float)i / 8.0f * 6.2832f;
        float hue = (float)i / 8.0f;
        Vec4 col(0.5f+0.5f*std::cos(hue*6.2832f), 0.5f+0.5f*std::cos((hue+0.333f)*6.2832f),
                 0.5f+0.5f*std::cos((hue+0.667f)*6.2832f), 1);
        auto sel = (i % 2 == 0) ? cubeM : cylM;
        MakeSceneObj(*m_Scene, "Obj"+std::to_string(i), sel,
                     Vec3(std::cos(a)*6, 0.0f, std::sin(a)*6), col,
                     Vec3(0.8f, 0.8f, 0.8f));
    }

    m_Scene->GetAllGameObjects(m_SceneObjects);
}

// ── 运行入口 ──

void FinalShowcaseApp::Run() {
    // 创建窗口
    m_Window = m_Factory.CreateWindow(1920, 1080, "FinalShowcase - Engine Demo");
    if (!m_Window) { std::fprintf(stderr, "Failed to create window\n"); return; }
    m_Window->GetContext()->Init();

    // 相机
    m_Camera = std::make_unique<PerspectiveCamera>(60.0f, 16.0f/9.0f, 0.1f, 500.0f);
    m_Camera->SetPosition(Vec3(0, 5, 15));
    m_Camera->LookAt(Vec3(0, 0, 0), Vec3(0, 1, 0));
    m_StoredYaw = 180.0f; m_StoredPitch = -15.0f;

    // 渲染器
    m_MeshRenderer = std::make_unique<MeshRenderer>(m_Factory, *m_Window->GetContext());
    m_MeshRenderer->SetCamera(m_Camera.get());

    auto pbrShader = m_Factory.CreateShader("assets/shaders/pbr_lit.vert", "assets/shaders/pbr_lit.frag");
    if (pbrShader) m_MeshRenderer->SetShader(pbrShader);

    // 阴影
    m_ShadowMapper = std::make_unique<Rendering::ShadowMapper>(*m_Window->GetContext());
    m_MeshRenderer->SetShadowMapper(m_ShadowMapper.get());
    m_MeshRenderer->SetShadowEnabled(true);

    // 光源 (PBR 需要多光源)
    m_MeshRenderer->ClearLights();
    m_MeshRenderer->AddLight({Vec3(5, 10, 5), Vec3(1, 0.95f, 0.8f), 2.0f});
    m_MeshRenderer->AddLight({Vec3(-5, 5, -3), Vec3(0.8f, 0.85f, 1.0f), 1.0f});
    m_MeshRenderer->SetAmbientColor(Vec3(0.05f, 0.05f, 0.08f));

    // 场景
    BuildScene();

    // UI
    auto ui = m_Factory.CreateUIManager();
    if (ui) UIManager::Init(std::move(ui), m_Window->GetNativeHandle(), m_Window->GetContext());

    // 输入
    m_InputMgr.Init(m_Window.get());

    std::printf("[FinalShowcase] Entering main loop\n");

    auto lastTime = std::chrono::high_resolution_clock::now();
    bool running = true;

    while (running && !m_Window->ShouldClose()) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        if (dt > 0.05f) dt = 0.05f;

        m_InputMgr.Update();
        HandleInput(dt);

        // 渲染
        auto* ctx = m_Window->GetContext();
        ctx->ClearColor(0.05f, 0.05f, 0.08f, 1.0f);

        m_Scene->GetAllGameObjects(m_SceneObjects);
        m_MeshRenderer->SetCamera(m_Camera.get());
        m_MeshRenderer->Render(m_SceneObjects);

        // 统计
        m_PerfWindow.Update(dt);

        // UI
        UIManager::BeginFrame();
        DrawDebugPanel();
        UIManager::EndFrame();
        UIManager::Render();

        m_Window->SwapBuffers();
        m_Window->PollEvents();
        m_FrameCount++;
    }

    std::printf("[FinalShowcase] Exiting\n");
}

// ── 输入 ──

void FinalShowcaseApp::HandleInput(float dt) {
    if (m_InputMgr.IsKeyPressed(Key::Escape)) running = false;

    float speed = m_CameraSpeed * dt;
    auto pos = m_Camera->GetPosition();

    if (m_InputMgr.IsKeyDown(KeyCode::W)) pos.z -= speed;
    if (m_InputMgr.IsKeyDown(KeyCode::S)) pos.z += speed;
    if (m_InputMgr.IsKeyDown(KeyCode::A)) pos.x -= speed;
    if (m_InputMgr.IsKeyDown(KeyCode::D)) pos.x += speed;
    if (m_InputMgr.IsKeyDown(KeyCode::Q)) pos.y -= speed;
    if (m_InputMgr.IsKeyDown(KeyCode::E)) pos.y += speed;

    m_Camera->SetPosition(pos);

    // 鼠标旋转相机
    double mx, my;
    if (glfwGetMouseButton((GLFWwindow*)m_Window->GetNativeHandle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        if (!m_RightDragging) {
            m_RightDragging = true;
            glfwGetCursorPos((GLFWwindow*)m_Window->GetNativeHandle(), &m_LastMouseX, &m_LastMouseY);
        }
        glfwGetCursorPos((GLFWwindow*)m_Window->GetNativeHandle(), &mx, &my);
        float dx = (float)(mx - m_LastMouseX) * m_MouseSensitivity;
        float dy = (float)(my - m_LastMouseY) * m_MouseSensitivity;
        m_StoredYaw += dx;
        m_StoredPitch += dy;
        m_StoredPitch = std::clamp(m_StoredPitch, -89.0f, 89.0f);
        m_Camera->SetRotation(m_StoredPitch, m_StoredYaw);
        m_LastMouseX = mx;
        m_LastMouseY = my;
    } else {
        m_RightDragging = false;
    }

    // 滚轮缩放
    // glfwSetScrollCallback handled elsewhere
}

// ── 调试面板 ──

void FinalShowcaseApp::DrawDebugPanel() {
    ImGui::Begin("FinalShowcase");
    ImGui::Text("FPS: %.1f", m_PerfWindow.GetFPS());
    ImGui::Text("Frame: %.3f ms", m_PerfWindow.GetFrameTime());
    ImGui::Text("Objects: %zu", m_SceneObjects.size());
    ImGui::Separator();
    ImGui::Text("Controls: WASD=Move, Q/E=Up/Down");
    ImGui::Text("Right-Drag=Look, ESC=Exit");
    ImGui::Text("Backend: OpenGL 4.6 + PBR + Reverse-Z");
    ImGui::End();
}

void FinalShowcaseApp::OnWindowResize(int w, int h) {
    m_Camera->SetAspectRatio((float)w / (float)h);
}

} // namespace Engine