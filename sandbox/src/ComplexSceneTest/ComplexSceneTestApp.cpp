#include "ComplexSceneTestApp.h"
#include <Engine/ConsoleCommandRegistry.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/RHI/IPrimitiveBatch.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/RenderResources/VertexArray.h>
#include <Engine/Core/RenderResources/VertexBuffer.h>
#include <Engine/MemoryTracker.h>
#include <Engine/OpenGL/OpenGLContext.h>
#include <Engine/Rendering/TextRenderer.h>
#include <Engine/UIManager.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <glm/glm.hpp>
#include <iostream>
#include <random>
#include <sstream>

#include <spdlog/spdlog.h>

namespace Engine {

// ── Helper: 创建带 MeshComponent 的 GameObject ──
static std::shared_ptr<GameObject>
MakeMeshObj(Scene &scene, const std::string &name, std::shared_ptr<Mesh> mesh,
            const Vec3 &pos, const Vec4 &color,
            const Vec3 &scale = Vec3(1, 1, 1)) {
  auto obj = std::make_shared<GameObject>(name);
  obj->GetTransform().SetPosition(pos);
  obj->GetTransform().SetScale(scale);
  auto *mc = obj->AddComponent<MeshComponent>();
  mc->SetMesh(std::move(mesh));
  mc->m_Color = color;
  scene.AddObject(obj);
  return obj;
}

ComplexSceneTestApp::ComplexSceneTestApp(IGraphicsFactory &factory)
    : m_Factory(factory), m_TextureManager(factory),
      m_SceneRenderer(factory, m_TextureManager),
      m_Camera(60.0f, 1280.0f / 720.0f, 0.1f, 200.0f), m_Scene("ComplexScene") {
  m_Window = m_Factory.CreateWindow(1280, 720, "Complex 3D Scene Test");
  m_InputManager.Init(m_Window.get());
  m_SceneRenderer.SetRenderContext(*m_Window->GetContext());

  m_Camera.SetPosition(Vec3(0, 15, 30));
  m_Camera.SetRotation(-20, 0);

  // 3D Shader
  m_3DShader = m_Factory.CreateShader("assets/shaders/3d_lit.vert",
                                      "assets/shaders/3d_lit.frag");

  auto *ctx = m_Window->GetContext();
  m_MeshRenderer = std::make_shared<MeshRenderer>(m_Factory, *ctx);
  m_MeshRenderer->SetShader(m_3DShader);
  m_MeshRenderer->SetCamera(&m_Camera);
  m_MeshRenderer->SetAmbientColor(Vec3(0.4f, 0.45f, 0.5f));
  // Depth only shader for pre-pass
  m_DepthShader = m_Factory.CreateShader("assets/shaders/depth_only.vert",
                                         "assets/shaders/depth_only.frag");
  m_MeshRenderer->SetDepthShader(m_DepthShader);

  // ── 粒子/贴花着色器 ──
  m_ParticleShader = m_Factory.CreateShader("assets/shaders/particle.vert",
                                            "assets/shaders/particle.frag");
  m_DecalShader = m_Factory.CreateShader("assets/shaders/particle.vert",
                                         "assets/shaders/particle.frag");
  // UI
  m_UIShader = m_Factory.CreateShader("assets/shaders/ui.vert",
                                      "assets/shaders/ui.frag");
  // ── 粒子发射器配置：火焰 ──
  {
    ParticleEmitterConfig cfg;
    cfg.position = Vec3(0, 1, 0);
    cfg.positionVar = Vec3(0.5, 0.2, 0.5);
    cfg.velocity = Vec3(0, 3, 0);
    cfg.velocityVar = Vec3(0.5, 1.0, 0.5);
    cfg.startColor = Vec4(1, 1, 0.3, 1);
    cfg.endColor = Vec4(1, 0.2, 0, 0);
    cfg.startSize = 0.6f;
    cfg.endSize = 0.1f;
    cfg.lifeTime = 1.5f;
    cfg.lifeVar = 0.5f;
    cfg.emitRate = 60.0f;
    cfg.maxParticles = 300;
    m_FireEmitter.SetConfig(cfg);
  }
  // 火花发射器
  {
    ParticleEmitterConfig cfg;
    cfg.position = Vec3(0, 1.5, 0);
    cfg.positionVar = Vec3(0.2, 0, 0.2);
    cfg.velocity = Vec3(0, 1, 0);
    cfg.velocityVar = Vec3(2, 3, 2);
    cfg.startColor = Vec4(1, 0.8, 0.5, 1);
    cfg.endColor = Vec4(1, 0.3, 0, 0);
    cfg.startSize = 0.15f;
    cfg.endSize = 0.02f;
    cfg.lifeTime = 1.0f;
    cfg.lifeVar = 0.5f;
    cfg.emitRate = 40.0f;
    cfg.maxParticles = 200;
    m_SparkEmitter.SetConfig(cfg);
  }

  // ── 地面贴花 ──
  // 随机散布一些"弹孔"贴花在地面上
  for (int i = 0; i < 30; ++i) {
    float angle = (float)i / 30.0f * 6.2832f;
    float dist = 5.0f + (float)(i % 10) * 1.5f;
    Vec3 pos(std::cos(angle) * dist, -1.8f, std::sin(angle) * dist);
    m_DecalSystem.AddDecal(pos, Vec3(0, 1, 0), nullptr, Vec3(0.8, 0.8, 0.8));
  }

  // Build scene
  BuildScene();

  // Init UI
  InitUI();

  // Init HUD
  m_GameHUD.Initialize(factory, *ctx);

  // Setup scene graphs
  RebuildSceneGraph();

  // Default visible panels
  m_PerfWindow.SetVisible(true);
  m_MemoryPanel.SetVisible(true);

  std::cout << "[ComplexSceneTest] Initialized with " << m_AllObjects.size()
            << " objects.\n";
  spdlog::info("ComplexSceneTest initialized with {} objects. Press ~ to open console.",
               m_AllObjects.size());
  spdlog::info("Controls: WASD=move, Q/E=up/down, RMB+drag=look around");
}

ComplexSceneTestApp::~ComplexSceneTestApp() {
  if (m_UIInitialized) {
    UIManager::Shutdown();
    m_UIInitialized = false;
  }
}

bool ComplexSceneTestApp::InitUI() {
  auto ui = m_Factory.CreateUIManager();
  if (!ui)
    return false;
  m_UIInitialized = UIManager::Init(std::move(ui), m_Window->GetNativeHandle(),
                                    m_Window->GetContext());
  return m_UIInitialized;
}

void ComplexSceneTestApp::BuildScene() {
  std::mt19937 rng(42);
  auto randFloat = [&](float min, float max) {
    return min + (max - min) * ((float)rng() / (float)UINT32_MAX);
  };

  // ── Ground plane ──
  auto groundMesh = std::make_shared<Mesh>(
      Mesh::CreatePlane(m_WorldRadius * 2.5f, m_WorldRadius * 2.5f));
  MakeMeshObj(m_Scene, "Ground", groundMesh, Vec3(0, -2, 0),
              Vec4(0.25f, 0.35f, 0.25f, 1.0f), Vec3(1, 1, 1));

  // ── Central tower ──
  auto cubeMesh = std::make_shared<Mesh>(Mesh::CreateCube(1.5f));
  auto sphereMesh = std::make_shared<Mesh>(Mesh::CreateSphere(0.8f, 16));
  auto cylMesh = std::make_shared<Mesh>(Mesh::CreateCylinder(0.6f, 1.8f, 16));

  for (int i = 0; i < 6; ++i) {
    float y = -1.0f + i * 1.8f;
    Vec4 col(0.8f + randFloat(0, 0.2f), 0.2f + randFloat(0, 0.3f),
             0.2f + randFloat(0, 0.3f), 1);
    auto obj = MakeMeshObj(m_Scene, "Tower" + std::to_string(i),
                           i % 2 == 0 ? cubeMesh : cylMesh, Vec3(0, y, 0), col);
    float s = 1.0f + i * 0.05f;
    obj->GetTransform().SetScale(Vec3(s, s, s));
    obj->GetTransform().SetRotation(0, i * 30.0f, 0);
    m_AllObjects.push_back(obj.get());
  }

  // ── Spiral columns ──
  for (int ring = 0; ring < 3; ++ring) {
    float radius = 6.0f + ring * 4.0f;
    int count = 6 + ring * 3;
    for (int i = 0; i < count; ++i) {
      float angle = (float)i / count * 6.2832f + ring * 0.5f;
      float x = std::cos(angle) * radius;
      float z = std::sin(angle) * radius;
      float height = 2.0f + randFloat(0, 4.0f);
      int stacks = (int)(height / 1.0f);
      for (int s = 0; s < stacks; ++s) {
        float y = -1.5f + s * 1.5f;
        auto m = std::make_shared<Mesh>(Mesh::CreateCube(0.8f));
        Vec4 col(0.3f + randFloat(0, 0.4f), 0.4f + randFloat(0, 0.4f),
                 0.7f + randFloat(0, 0.3f), 1);
        auto obj = MakeMeshObj(m_Scene,
                               "Col" + std::to_string(ring) + "_" +
                                   std::to_string(i) + "_" + std::to_string(s),
                               m, Vec3(x, y, z), col);
        m_AllObjects.push_back(obj.get());
      }
    }
  }

  // ── Random scattered objects ──
  for (int i = 0; i < m_ObjectCount; ++i) {
    float angle = randFloat(0, 6.2832f);
    float dist = randFloat(3.0f, m_WorldRadius);
    float x = std::cos(angle) * dist;
    float z = std::sin(angle) * dist;
    float y = -1.0f + randFloat(0, 0.5f);

    int type = (int)(randFloat(0, 3));
    std::shared_ptr<Mesh> mesh;
    if (type == 0)
      mesh = std::make_shared<Mesh>(Mesh::CreateCube(randFloat(0.3f, 1.5f)));
    else if (type == 1)
      mesh =
          std::make_shared<Mesh>(Mesh::CreateSphere(randFloat(0.3f, 1.0f), 12));
    else
      mesh = std::make_shared<Mesh>(Mesh::CreateCylinder(
          randFloat(0.2f, 0.8f), randFloat(0.5f, 2.0f), 12));

    Vec4 col(randFloat(0.2f, 1.0f), randFloat(0.2f, 1.0f),
             randFloat(0.2f, 1.0f), 1);
    auto name = "Obj" + std::to_string(i);
    auto obj = MakeMeshObj(m_Scene, name, mesh, Vec3(x, y, z), col);
    obj->GetTransform().SetRotation(randFloat(0, 360), randFloat(0, 360), 0);
    m_AllObjects.push_back(obj.get());
  }

  // ── Corner towers (large landmark structures) ──
  Vec3 corners[] = {
      Vec3(-m_WorldRadius * 0.7f, -1, -m_WorldRadius * 0.7f),
      Vec3(m_WorldRadius * 0.7f, -1, -m_WorldRadius * 0.7f),
      Vec3(-m_WorldRadius * 0.7f, -1, m_WorldRadius * 0.7f),
      Vec3(m_WorldRadius * 0.7f, -1, m_WorldRadius * 0.7f),
  };
  for (int ci = 0; ci < 4; ++ci) {
    float h = 5.0f + randFloat(0, 5.0f);
    for (int s = 0; s < (int)(h / 1.5f); ++s) {
      auto m = std::make_shared<Mesh>(Mesh::CreateCube(2.0f));
      auto obj = MakeMeshObj(
          m_Scene, "Landmark" + std::to_string(ci) + "_" + std::to_string(s), m,
          Vec3(corners[ci].x, corners[ci].y + s * 1.5f, corners[ci].z),
          Vec4(0.6f, 0.3f, 0.2f, 1));
      m_AllObjects.push_back(obj.get());
    }
  }

  std::cout << "[ComplexSceneTest] Built scene: "
            << m_Scene.GetTotalObjectCount() << " total objects.\n";
}

void ComplexSceneTestApp::RebuildSceneGraph() {
  m_AllObjects.clear();
  for (auto &obj : m_Scene.GetObjects())
    m_AllObjects.push_back(obj.get());

  // Build all three graph types
  m_FlatGraph.Build(m_AllObjects);
  m_BVHGraph.Build(m_AllObjects);
  m_GridGraph.Build(m_AllObjects);

  // Select active
  switch (m_SelectedGraphType) {
  case 0:
    m_ActiveGraph = &m_FlatGraph;
    break;
  case 1:
    m_ActiveGraph = &m_BVHGraph;
    break;
  case 2:
    m_ActiveGraph = &m_GridGraph;
    break;
  default:
    m_ActiveGraph = &m_BVHGraph;
    break;
  }

  m_MeshRenderer->SetSceneGraph(m_ActiveGraph);
  m_MeshRenderer->SetSceneGraphThreshold((uint32)m_SceneGraphThreshold);
  m_MeshRenderer->SetDepthPrePassEnabled(m_EnableDepthPrePass);

  std::cout << "[ComplexSceneTest] Scene graph: " << m_ActiveGraph->GetName()
            << " (" << m_AllObjects.size() << " objects)\n";
}

void ComplexSceneTestApp::UpdateScene(float dt) {
  static float rotAngle = 0.0f;
  rotAngle += dt * 20.0f;

  // Update scene graph frustum
  m_ActiveGraph->SetFrustum(&m_Camera);
}

void ComplexSceneTestApp::HandleInput(float dt) {
  // ── 控制台切换（~ 键） ──
  if (Input::IsKeyPressed(KeyCode::GraveAccent)) {
    m_ConsolePanel.ToggleVisibility();
  }

  // 控制台捕获输入时阻止游戏操作
  bool consoleCaptures = m_ConsolePanel.IsCapturingInput();
  Input::SetBlockInput(consoleCaptures, consoleCaptures);
  if (consoleCaptures)
    return;

  // ── 摄像机移动（WASD + Q/E） ──
  Vec3 pos = m_Camera.GetPosition();
  float speed = m_CameraSpeed * dt;
  Vec3 fwd = m_Camera.GetForward();
  Vec3 right = m_Camera.GetRight();

  if (Input::IsKeyDown(KeyCode::W)) {
    pos.x += fwd.x * speed;
    pos.y += fwd.y * speed;
    pos.z += fwd.z * speed;
  }
  if (Input::IsKeyDown(KeyCode::S)) {
    pos.x -= fwd.x * speed;
    pos.y -= fwd.y * speed;
    pos.z -= fwd.z * speed;
  }
  if (Input::IsKeyDown(KeyCode::A)) {
    pos.x -= right.x * speed;
    pos.y -= right.y * speed;
    pos.z -= right.z * speed;
  }
  if (Input::IsKeyDown(KeyCode::D)) {
    pos.x += right.x * speed;
    pos.y += right.y * speed;
    pos.z += right.z * speed;
  }
  if (Input::IsKeyDown(KeyCode::Q))
    pos.y -= speed;
  if (Input::IsKeyDown(KeyCode::E))
    pos.y += speed;
  m_Camera.SetPosition(pos);

  // ── 自由视角旋转 ──
  // 鼠标滚轮键 / 右键拖拽自由旋转，箭头键备选
  float yaw = m_Camera.GetYaw();
  float pitch = m_Camera.GetPitch();

  // 鼠标自由视角（按下右键或中键时）
  if (Input::IsMouseButtonDown(MouseCode::ButtonRight) ||
      Input::IsMouseButtonDown(MouseCode::ButtonMiddle)) {
    float dx = Input::GetMouseDeltaX();
    float dy = Input::GetMouseDeltaY();
    yaw   += dx * m_MouseSensitivity;
    pitch += dy * m_MouseSensitivity * 0.8f; // Y 轴灵敏度略低
  }

  // 箭头键备选
  if (Input::IsKeyDown(KeyCode::Left))
    yaw -= 60.0f * dt;
  if (Input::IsKeyDown(KeyCode::Right))
    yaw += 60.0f * dt;
  if (Input::IsKeyDown(KeyCode::Up))
    pitch += 60.0f * dt;
  if (Input::IsKeyDown(KeyCode::Down))
    pitch -= 60.0f * dt;

  // 限制俯仰角防止翻转
  pitch = std::max(-89.0f, std::min(89.0f, pitch));
  m_Camera.SetRotation(pitch, yaw);
}

void ComplexSceneTestApp::DrawDebugImGui() {
  if (!m_UIInitialized)
    return;

  ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 600), ImGuiCond_FirstUseEver);

  ImGui::Begin("Complex Scene Debug", nullptr,
               ImGuiWindowFlags_NoSavedSettings);

  // ══════════════════════════════════════
  // Scene Info
  // ══════════════════════════════════════
  if (ImGui::CollapsingHeader("Scene Info", ImGuiTreeNodeFlags_DefaultOpen)) {
    Vec3 camPos = m_Camera.GetPosition();
    ImGui::Text("Camera: (%.1f, %.1f, %.1f)", camPos.x, camPos.y, camPos.z);
    ImGui::Text("Pitch: %.1f | Yaw: %.1f", m_Camera.GetPitch(),
                m_Camera.GetYaw());
    ImGui::Text("Total Objects: %zu", m_Scene.GetTotalObjectCount());
    ImGui::Text("Mesh Objects: %zu", m_AllObjects.size());
    ImGui::Separator();

    auto sgStats =
        m_ActiveGraph ? m_ActiveGraph->GetStats() : SceneGraphStats{};
    ImGui::Text("Scene Graph: %s",
                m_ActiveGraph ? m_ActiveGraph->GetName() : "None");
    ImGui::Text("Visible: %u / %u (%.1f%%)", sgStats.visibleCount,
                sgStats.totalObjects, sgStats.cullRatio * 100);
    ImGui::Text("Culled: %u objects",
                sgStats.totalObjects - sgStats.visibleCount);
    if (sgStats.nodeCount > 0)
      ImGui::Text("Internal Nodes: %u", sgStats.nodeCount);
  }

  // ══════════════════════════════════════
  // Scene Graph
  // ══════════════════════════════════════
  if (ImGui::CollapsingHeader("Scene Graph", ImGuiTreeNodeFlags_DefaultOpen)) {
    int prevType = m_SelectedGraphType;
    ImGui::Combo("Type", &m_SelectedGraphType, kGraphTypeNames);

    ImGui::Checkbox("Enable Culling", &m_EnableSceneGraph);
    ImGui::SliderInt("Threshold", &m_SceneGraphThreshold, 10, 500);

    bool rebuild = (prevType != m_SelectedGraphType);
    rebuild |= ImGui::SliderInt("Object Count", &m_ObjectCount, 20, 500);

    if (rebuild) {
      if (prevType != m_SelectedGraphType) {
        RebuildSceneGraph();
      } else {
        ImGui::TextColored(ImVec4(1, 1, 0, 1),
                           "Object count change requires restart.");
      }
    }

    if (ImGui::Button("Rebuild Scene Graph")) {
      RebuildSceneGraph();
    }
  }

  // ══════════════════════════════════════
  // Render Settings
  // ══════════════════════════════════════
  if (ImGui::CollapsingHeader("Render Settings",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Checkbox("Depth Pre-Pass", &m_EnableDepthPrePass)) {
      m_MeshRenderer->SetDepthPrePassEnabled(m_EnableDepthPrePass);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Rotate Objects", &m_RotateObjects);
    ImGui::Checkbox("Animate Light", &m_AnimateLights);

    Vec3 ambient = m_MeshRenderer->GetAmbientColor();
    if (ImGui::ColorEdit3("Ambient", &ambient.x)) {
      m_MeshRenderer->SetAmbientColor(ambient);
    }
  }

  // ══════════════════════════════════════
  // Anti-Aliasing
  // ══════════════════════════════════════
  if (ImGui::CollapsingHeader("Anti-Aliasing",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    const char *aaModes[] = {"None", "MSAA", "SSAA", "CSAA", "MLAA"};
    int prevMode = m_CurrentAAMode;
    int prevSample = m_AASampleIndex;

    ImGui::Combo("Mode", &m_CurrentAAMode, aaModes, IM_ARRAYSIZE(aaModes));
    if (m_CurrentAAMode >= 1 && m_CurrentAAMode <= 3) {
      const char *samples[] = {"2x", "4x", "8x"};
      ImGui::Combo("Samples", &m_AASampleIndex, samples, 3);
    }

    if (m_CurrentAAMode == 2) { // SSAA
      ImGui::SliderFloat("SSAA Scale", &m_AASCale, 1.0f, 4.0f, "%.1fx");
    }

    if (prevMode != m_CurrentAAMode || prevSample != m_AASampleIndex) {
      m_AADemoConfig.sampleCount =
          (m_CurrentAAMode >= 1 && m_CurrentAAMode <= 3)
              ? k_SampleValues[m_AASampleIndex]
              : 4;
      switch (m_CurrentAAMode) {
      case 0:
        m_AADemoConfig.mode = AntiAliasingMode::None;
        break;
      case 1:
        m_AADemoConfig.mode = AntiAliasingMode::MSAA;
        break;
      case 2:
        m_AADemoConfig.mode = AntiAliasingMode::SSAA;
        break;
      case 3:
        m_AADemoConfig.mode = AntiAliasingMode::CSAA;
        break;
      case 4:
        m_AADemoConfig.mode = AntiAliasingMode::MLAA;
        break;
      }
      m_Factory.SetAntiAliasingConfig(m_AADemoConfig);
    }

    ImGui::Text("Active: %s %dx",
                m_AADemoConfig.mode == AntiAliasingMode::None ? "Disabled"
                                                              : "Enabled",
                m_AADemoConfig.sampleCount);
  }

  // ══════════════════════════════════════
  // Debug Tools
  // ══════════════════════════════════════
  if (ImGui::CollapsingHeader("Debug Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
    bool perfVis = m_PerfWindow.IsVisible();
    if (ImGui::Checkbox("Performance Window", &perfVis))
      m_PerfWindow.SetVisible(perfVis);

    bool memVis = m_MemoryPanel.IsVisible();
    if (ImGui::Checkbox("Memory Panel", &memVis))
      m_MemoryPanel.SetVisible(memVis);

    ImGui::Checkbox("Show HUD", &m_HUDVisible);

    ImGui::Separator();
    ImGui::Checkbox("Particles", &m_ParticlesEnabled);
    ImGui::Checkbox("Decals", &m_DecalsEnabled);

    int pCount =
        m_FireEmitter.GetActiveCount() + m_SparkEmitter.GetActiveCount();
    ImGui::Text("Particles: %d active", pCount);
    ImGui::Text("Decals: %d placed", m_DecalSystem.GetCount());

    ImGui::Text("Toggle Console: ~ (Grave Accent)");
  }

  ImGui::End();
}

void ComplexSceneTestApp::Run() {
  using namespace std::chrono;
  auto last = high_resolution_clock::now();

  while (!m_Window->ShouldClose()) {
    auto now = high_resolution_clock::now();
    float dt = duration<float>(now - last).count();
    last = now;
    if (dt > 0.05f)
      dt = 0.05f;

    m_Window->PollEvents();
    HandleInput(dt);
    MemoryTracker::FrameStart();

    // ── 检测窗口 resize，更新 HUD 屏幕尺寸 ──
    {
      int w, h;
      glfwGetFramebufferSize(
          static_cast<GLFWwindow *>(m_Window->GetNativeHandle()), &w, &h);
      static int lastW = 0, lastH = 0;
      if (w != lastW || h != lastH) {
        lastW = w;
        lastH = h;
        if (w > 0 && h > 0)
          OnWindowResize(w, h);
      }
    }

    // Update scene
    UpdateScene(dt);

    if (m_AnimateLights) {
      m_LightAngle += dt * 15.0f;
      float rad = m_LightAngle * 3.14159f / 180.0f;
      Vec3 lightPos(std::cos(rad) * 30.0f, 25.0f, std::sin(rad) * 30.0f);
      m_MeshRenderer->SetLightPosition(lightPos);
    }

    // ── 粒子/贴花更新 ──
    m_FireEmitter.Update(dt);
    m_SparkEmitter.Update(dt);
    m_DecalSystem.Update(dt);

    // ── Render ──
    auto *ctx = m_Window->GetContext();
    if (ctx) {
      ctx->ClearColor(0.05f, 0.05f, 0.10f, 1.0f);

      // 3D 场景渲染
      m_MeshRenderer->RenderWithSceneGraph(
          m_AllObjects,
          m_EnableSceneGraph ? &m_ActiveGraph->GetFrustum() : nullptr);

      // 粒子渲染
      if (m_ParticlesEnabled && m_ParticleShader) {
        const Mat4 &v = m_Camera.GetViewMatrix();
        const Mat4 &p = m_Camera.GetProjectionMatrix();
        ctx->SetDepthMask(false);
        m_FireEmitter.Render(*m_MeshRenderer->GetBatch(),
                             m_ParticleShader.get(), v, p);
        m_SparkEmitter.Render(*m_MeshRenderer->GetBatch(),
                              m_ParticleShader.get(), v, p);
        ctx->SetDepthMask(true);
      }

      // 贴花渲染
      if (m_DecalsEnabled && m_DecalShader) {
        const Mat4 &v = m_Camera.GetViewMatrix();
        const Mat4 &p = m_Camera.GetProjectionMatrix();
        m_DecalSystem.Render(*m_MeshRenderer->GetBatch(), m_DecalShader.get(),
                             &m_Camera);
      }

      // ═══ 手动 resolve：AA FBO → 默认帧缓冲 ═══
      static_cast<OpenGLContext *>(ctx)->ResolveToDefault();
    }

    // ── HUD 渲染（默认帧缓冲，不被 AA 处理） ──
    if (m_HUDVisible) {
      auto *hudCtx = m_Window->GetContext();
      if (hudCtx) {
        auto &gl = static_cast<OpenGLContext *>(hudCtx)->GetGL();
        // 只负责绑定字体纹理，GameHUD::Render 会设置着色器和投影
        uint32 fontTex = m_GameHUD.GetTextRenderer().GetFontTextureID();

        if (fontTex && gl.BindTextureUnit) {
          gl.BindTextureUnit(0, fontTex);
        } else if (fontTex) {
          gl.ActiveTexture(GL_TEXTURE0);
          gl.BindTexture(GL_TEXTURE_2D, fontTex);
        }

        gl.Disable(GL_DEPTH_TEST);
        gl.Enable(GL_BLEND);
        gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        auto *batch = m_MeshRenderer->GetBatch();
        if (batch) {
          batch->Begin(PrimitiveType::Triangles);
          m_GameHUD.Render(*batch);
          batch->End();
        }

        gl.Disable(GL_BLEND);
        gl.Enable(GL_DEPTH_TEST);
      }
    }

    // ── Performance stats ──
    uint32 drawCalls = ctx ? ctx->GetAndResetDrawCallCount() : 0;
    m_PerfWindow.FeedStats(dt * 1000.0f, drawCalls, (uint32)m_AllObjects.size(),
                           0, 0, 0);

    // ── HUD 数据更新 ──
    {
      m_GameHUD.SetFPS(1.0f / std::max(dt, 0.0001f));
      m_GameHUD.SetFrameTime(dt * 1000.0f);
      m_GameHUD.SetDrawCalls(drawCalls);
      m_GameHUD.SetObjectCount((uint32)m_AllObjects.size());
      m_GameHUD.SetVisibleCount(m_ActiveGraph
                                    ? m_ActiveGraph->GetStats().visibleCount
                                    : (uint32)m_AllObjects.size());
      m_GameHUD.SetMemoryUsage(MemoryTracker::GetCurrentUsage());
      m_GameHUD.SetPosition(m_Camera.GetPosition());
      m_GameHUD.SetVisible(m_HUDVisible);
    }

    // ── ImGui（渲染到默认帧缓冲） ──
    if (m_UIInitialized && UIManager::Get()) {
      UIManager::Begin();
      m_PerfWindow.OnImGui();
      DrawDebugImGui();
      m_ConsolePanel.OnImGui();
      m_MemoryPanel.OnImGui();
      UIManager::End();

      ImGuiIO &io = ImGui::GetIO();
      Input::SetBlockInput(io.WantCaptureMouse, io.WantCaptureKeyboard);
    }

    MemoryTracker::FrameEnd();
    m_InputManager.OnUpdate();

    // ── 仅交换（已在上面手动 resolve 过） ──
    glfwSwapBuffers(static_cast<GLFWwindow *>(m_Window->GetNativeHandle()));

    // 每 120 帧打印一次性能日志到控制台
    static int frameCount = 0;
    if (++frameCount % 120 == 0) {
      spdlog::info("[Perf] FPS: {:.1f} | DrawCalls: {} | Objects: {}", 1.0f / std::max(dt, 0.0001f), drawCalls, m_AllObjects.size());
    }
  }
}

void ComplexSceneTestApp::OnWindowResize(int width, int height) {
  // 更新相机宽高比
  float aspect = (float)width / (float)height;
  m_Camera.SetAspectRatio(aspect);

  // 更新 HUD 屏幕尺寸
  m_GameHUD.UpdateScreenSize((float)width, (float)height);
  if (auto *ctx = m_Window->GetContext()) {
    ctx->OnResize(width, height);
  }
  std::cout << "[ComplexSceneTest] Resized to " << width << "x" << height
            << "\n";
}

} // namespace Engine
