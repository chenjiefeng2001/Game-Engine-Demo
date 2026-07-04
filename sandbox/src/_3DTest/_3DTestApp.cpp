#include "_3DTestApp.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/MemoryTracker.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/OpenGL/OpenGLGBuffer.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <cmath>
#include <random>

namespace Engine {

static GameObject *MakeMeshObj(Scene &scene, const std::string &name,
                               std::shared_ptr<Mesh> mesh, const Vec3 &pos,
                               const Vec4 &color,
                               const Vec3 &scale = Vec3(1, 1, 1)) {
  auto obj = std::make_shared<GameObject>(name);
  obj->GetTransform().SetPosition(pos);
  obj->GetTransform().SetScale(scale);
  auto *mc = obj->AddComponent<MeshComponent>();
  mc->SetMesh(std::move(mesh));
  mc->m_Color = color;
  scene.AddObject(obj);
  return obj.get();
}

_3DTestApp::_3DTestApp(IGraphicsFactory &factory, const char* title)
    : m_Factory(factory), m_TextureManager(factory),
      m_Camera(60.0f, 1280.0f / 720.0f, 0.1f, 200.0f), 
      m_Scene(title ? title : "DebugScene"),
      m_AppTitle(title) {
  m_GameStarted = false;
  m_AnimateLights = true;
  m_LightAngle = 0.0f;

  m_Window = m_Factory.CreateWindow(1280, 720, m_AppTitle);
  m_InputManager.Init(m_Window.get());
  m_Camera.SetPosition(Vec3(0, 10, 25));
  m_Camera.SetRotation(-15, 0);

  auto *ctx = m_Window->GetContext();
  m_MeshRenderer = std::make_unique<MeshRenderer>(m_Factory, *ctx);
  m_MeshRenderer->SetCamera(&m_Camera);
  m_MeshRenderer->SetAmbientColor(Vec3(0.2f, 0.2f, 0.25f));
  m_MeshRenderer->SetDepthPrePassEnabled(true);

  m_3DShader = m_Factory.CreateShader("assets/shaders/3d_lit.vert",
                                      "assets/shaders/3d_lit.frag");
  m_MeshRenderer->SetShader(m_3DShader);
  m_DepthShader = m_Factory.CreateShader("assets/shaders/depth_only.vert",
                                         "assets/shaders/depth_only.frag");
  m_MeshRenderer->SetDepthShader(m_DepthShader);

  // ShadowMapper 是纯抽象接口，暂未有具体实现，注释掉以允许编译
  // m_ShadowMapper = std::make_unique<ShadowMapper>(*ctx);
  // m_MeshRenderer->SetShadowMapper(m_ShadowMapper.get());
  m_MeshRenderer->SetShadowEnabled(false);
  m_MeshRenderer->AddLight({{15, 20, 15}, {1, 1, 1}, 1.5f});
  m_MeshRenderer->AddLight({{-10, 8, 12}, {0.8f, 0.6f, 1.0f}, 0.8f});
  m_MeshRenderer->AddLight({{5, 3, -12}, {1.0f, 0.4f, 0.2f}, 0.6f});

  m_PerfWindow.SetRenderContext(ctx);

  BuildScene();
  m_ConsolePanel.SetVisible(false);

  // ── DebugLightState 初始化 ──
  m_DebugLights.clear();
  for (size_t i = 0; i < m_MeshRenderer->GetLightCount(); ++i) {
      const auto& light = m_MeshRenderer->GetLight(i);
      m_DebugLights.push_back({ light.position, light.color, light.intensity, true });
  }

  // ── 初始化新渲染调试状态 ──
  InitDeferredRendering();
}

_3DTestApp::~_3DTestApp() {
  if (m_UIInitialized) {
    UIManager::Shutdown();
    m_UIInitialized = false;
  }
}

bool _3DTestApp::InitUI() {
  auto ui = m_Factory.CreateUIManager();
  if (!ui)
    return false;
  m_UIInitialized = UIManager::Init(std::move(ui), m_Window->GetNativeHandle(),
                                    m_Window->GetContext());
  return m_UIInitialized;
}

void _3DTestApp::BuildScene() {
  std::mt19937 rng(42);
  auto rf = [&](float lo, float hi) {
    return lo + (hi - lo) * ((float)rng() / (float)rng.max());
  };

  auto groundMesh = std::make_shared<Mesh>(Mesh::CreatePlane(60, 60));
  auto *g = MakeMeshObj(m_Scene, "Ground", groundMesh, Vec3(0, -2, 0),
                        Vec4(0.3f, 0.35f, 0.3f, 1), Vec3(1, 1, 1));
  if (g)
    m_SceneObjects.push_back(g);

  auto cubeMesh = std::make_shared<Mesh>(Mesh::CreateCube(1.5f));
  auto sphereMesh = std::make_shared<Mesh>(Mesh::CreateSphere(0.8f, 16));
  auto cylMesh = std::make_shared<Mesh>(Mesh::CreateCylinder(0.6f, 1.8f, 16));
  for (int i = 0; i < 6; ++i) {
    float y = -1.0f + i * 1.8f;
    Vec4 col(0.8f + rf(0, 0.2f), 0.2f + rf(0, 0.3f), 0.2f + rf(0, 0.3f), 1);
    auto *obj = MakeMeshObj(m_Scene, "Tower" + std::to_string(i),
                            i % 2 == 0 ? cubeMesh : cylMesh, Vec3(0, y, 0), col);
    obj->GetTransform().SetScale(Vec3(1 + i * 0.05f, 1 + i * 0.05f, 1 + i * 0.05f));
    obj->GetTransform().SetRotation(0, i * 30.0f, 0);
    m_SceneObjects.push_back(obj);
    m_Colliders.push_back(obj);
  }

  auto movingMesh = std::make_shared<Mesh>(Mesh::CreateSphere(0.8f, 16));
  auto movingObj = std::make_shared<GameObject>("MovingBall");
  movingObj->GetTransform().SetPosition(Vec3(0, -0.5f, 0));
  m_MovingMeshComp = movingObj->AddComponent<MeshComponent>();
  m_MovingMeshComp->SetMesh(std::move(movingMesh));
  m_MovingMeshComp->m_Color = Vec4(0.2f, 0.8f, 0.2f, 1.0f);
  m_Scene.AddObject(movingObj);
  m_MovingObj = movingObj.get();
  m_SceneObjects.push_back(m_MovingObj);

  for (int ring = 0; ring < 3; ++ring) {
    float radius = 6.0f + ring * 4.0f;
    int cnt = 6 + ring * 2;
    for (int i = 0; i < cnt; ++i) {
      float angle = (float)i / cnt * 6.2832f + ring * 0.5f;
      float x = std::cos(angle) * radius, z = std::sin(angle) * radius;
      int stacks = 2 + ring;
      for (int s = 0; s < stacks; ++s) {
        float y = -1.5f + s * 1.5f;
        auto m = std::make_shared<Mesh>(Mesh::CreateCube(0.8f));
        Vec4 col(0.3f + rf(0, 0.4f), 0.4f + rf(0, 0.4f), 0.7f + rf(0, 0.3f), 1);
        auto *obj = MakeMeshObj(m_Scene, "Ring" + std::to_string(ring) + "_" +
                                    std::to_string(i) + "_" + std::to_string(s),
                                m, Vec3(x, y, z), col);
        m_SceneObjects.push_back(obj);
      }
    }
  }

  for (int i = 0; i < 80; ++i) {
    float angle = rf(0, 6.2832f), dist = rf(3, 18);
    float x = std::cos(angle) * dist, z = std::sin(angle) * dist;
    float y = -1.0f + rf(0, 0.5f);
    int type = (int)rf(0, 3);
    std::shared_ptr<Mesh> m;
    if (type == 0) m = std::make_shared<Mesh>(Mesh::CreateCube(rf(0.3f, 1.2f)));
    else if (type == 1) m = std::make_shared<Mesh>(Mesh::CreateSphere(rf(0.3f, 0.8f), 12));
    else m = std::make_shared<Mesh>(Mesh::CreateCylinder(rf(0.2f, 0.6f), rf(0.5f, 1.5f), 12));
    Vec4 col(rf(0.2f, 1), rf(0.2f, 1), rf(0.2f, 1), 1);
    auto *obj = MakeMeshObj(m_Scene, "Obj" + std::to_string(i), m, Vec3(x, y, z), col);
    obj->GetTransform().SetRotation(rf(0, 360), rf(0, 360), 0);
    m_SceneObjects.push_back(obj);
  }

  Vec3 corners[] = {{-14, -1, -14}, {14, -1, -14}, {-14, -1, 14}, {14, -1, 14}};
  for (int c = 0; c < 4; ++c) {
    float h = 5.0f + rf(0, 4);
    for (int s = 0; s < (int)(h / 1.5f); ++s) {
      auto m = std::make_shared<Mesh>(Mesh::CreateCube(1.8f));
      auto *obj = MakeMeshObj(m_Scene, "Landmark" + std::to_string(c) + "_" + std::to_string(s), m,
          Vec3(corners[c].x, corners[c].y + s * 1.5f, corners[c].z),
          Vec4(0.6f, 0.3f, 0.2f, 1));
      m_SceneObjects.push_back(obj);
    }
  }
}

void _3DTestApp::UpdateLogic(float dt) {
  if (!m_MovingObj) return;

  Vec3 pos = m_MovingObj->GetTransform().GetPosition();
  pos.x += m_MovingVelocity.x * dt;
  pos.y += m_MovingVelocity.y * dt;
  pos.z += m_MovingVelocity.z * dt;

  bool collided = false;

  if (pos.x < -13.0f) { pos.x = -13.0f; m_MovingVelocity.x *= -1.0f; collided = true; }
  if (pos.x >  13.0f) { pos.x =  13.0f; m_MovingVelocity.x *= -1.0f; collided = true; }
  if (pos.z < -13.0f) { pos.z = -13.0f; m_MovingVelocity.z *= -1.0f; collided = true; }
  if (pos.z >  13.0f) { pos.z =  13.0f; m_MovingVelocity.z *= -1.0f; collided = true; }

  for (auto* obj : m_Colliders) {
      Vec3 tPos = obj->GetTransform().GetPosition();
      float dx = pos.x - tPos.x;
      float dz = pos.z - tPos.z;
      float distSq = dx * dx + dz * dz;

      if (distSq > 0.0001f && distSq < 5.0f) {
          float dist = std::sqrt(distSq);
          collided = true;
          
          float nx = dx / dist;
          float nz = dz / dist;

          float dot = m_MovingVelocity.x * nx + m_MovingVelocity.z * nz;
          if (dot < 0) {
              m_MovingVelocity.x -= 2.0f * dot * nx;
              m_MovingVelocity.z -= 2.0f * dot * nz;
          }
          pos.x = tPos.x + nx * 2.25f;
          pos.z = tPos.z + nz * 2.25f;
      }
  }

  m_MovingObj->GetTransform().SetPosition(pos);

  if (m_MovingMeshComp) {
      if (collided) {
          m_MovingMeshComp->m_Color = Vec4(1.0f, 0.2f, 0.2f, 1.0f);
      } else {
          m_MovingMeshComp->m_Color.x += (0.2f - m_MovingMeshComp->m_Color.x) * dt * 5.0f;
          m_MovingMeshComp->m_Color.y += (0.8f - m_MovingMeshComp->m_Color.y) * dt * 5.0f;
          m_MovingMeshComp->m_Color.z += (0.2f - m_MovingMeshComp->m_Color.z) * dt * 5.0f;
      }
  }
}

void _3DTestApp::HandleInput(float dt) {
  auto* glfwWin = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());

  static bool wasF3 = false;
  bool isF3 = glfwGetKey(glfwWin, GLFW_KEY_F3) == GLFW_PRESS;
  if (isF3 && !wasF3)
    m_PerfWindow.ToggleVisibility();
  wasF3 = isF3;

  static bool wasTilde = false;
  bool isTilde = glfwGetKey(glfwWin, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS;
  if (isTilde && !wasTilde) m_ConsolePanel.ToggleVisibility();
  wasTilde = isTilde;

  if (m_ConsolePanel.IsCapturingInput())
    return;

  if (m_MenuManager.isVisible) {
    if (Input::IsKeyPressed(KeyCode::Up)) m_MenuManager.OnKeyPressed(KeyCode::Up);
    if (Input::IsKeyPressed(KeyCode::Down)) m_MenuManager.OnKeyPressed(KeyCode::Down);
    if (Input::IsKeyPressed(KeyCode::Enter)) m_MenuManager.OnKeyPressed(KeyCode::Enter);
    if (Input::IsKeyPressed(KeyCode::Escape)) {
      if (m_MenuManager.currentPage == MenuManager::Page::Pause) {
        m_MenuManager.isVisible = false;
        m_MenuManager.currentPage = MenuManager::Page::None;
      } else {
        m_MenuManager.OnKeyPressed(KeyCode::Escape);
      }
    }
    return;
  }

  if (Input::IsKeyPressed(KeyCode::Escape)) {
    m_MenuManager.isVisible = true;
    m_MenuManager.currentPage = MenuManager::Page::Pause;
    return;
  }

  bool wantCaptureKeyboard = UIManager::WantCaptureKeyboard();
  bool wantCaptureMouse = UIManager::WantCaptureMouse();
  bool rightMouseDown = Input::IsMouseButtonDown(MouseCode::ButtonRight);

  auto *ctx = m_Window->GetContext();
  if (ctx && !wantCaptureKeyboard) {
    auto *ht = ctx->GetHelperTogglesData();
    if (ht) {
      if (Input::IsKeyPressed(KeyCode::F1)) ht->showGrid = !ht->showGrid;
      if (Input::IsKeyPressed(KeyCode::F2)) ht->showColliders = !ht->showColliders;
      if (Input::IsKeyPressed(KeyCode::F10)) {
        ht->pauseRendering = !ht->pauseRendering;
        if (ht->pauseRendering)
          ht->stepOneFrame = false;
      }
      if (Input::IsKeyPressed(KeyCode::F11)) {
        ht->stepOneFrame = true;
        ht->pauseRendering = false;
      }
    }
  }

  if (!wantCaptureKeyboard) {
    Vec3 pos = m_Camera.GetPosition();
    float s = m_CameraSpeed * dt;
    Vec3 fwd = m_Camera.GetForward(), right = m_Camera.GetRight();
    if (Input::IsKeyDown(KeyCode::W)) { pos.x += fwd.x * s; pos.y += fwd.y * s; pos.z += fwd.z * s; }
    if (Input::IsKeyDown(KeyCode::S)) { pos.x -= fwd.x * s; pos.y -= fwd.y * s; pos.z -= fwd.z * s; }
    if (Input::IsKeyDown(KeyCode::A)) { pos.x -= right.x * s; pos.y -= right.y * s; pos.z -= right.z * s; }
    if (Input::IsKeyDown(KeyCode::D)) { pos.x += right.x * s; pos.y += right.y * s; pos.z += right.z * s; }
    if (Input::IsKeyDown(KeyCode::Q)) pos.y -= s;
    if (Input::IsKeyDown(KeyCode::E)) pos.y += s;
    m_Camera.SetPosition(pos);
  }

  if (!wantCaptureMouse || rightMouseDown) {
    float yaw = m_Camera.GetYaw(), pitch = m_Camera.GetPitch();
    if (rightMouseDown) {
      yaw += Input::GetMouseDeltaX() * m_MouseSensitivity;
      pitch += Input::GetMouseDeltaY() * m_MouseSensitivity * 0.8f;
    }
    pitch = std::max(-89.0f, std::min(89.0f, pitch));
    m_Camera.SetRotation(pitch, yaw);
  }
}

void _3DTestApp::DrawDebugImGui() {
  if (!m_UIInitialized)
    return;

  std::string windowTitle = std::string(m_AppTitle ? m_AppTitle : "Debug") + " Info";
  
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::Begin(windowTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1), "%s", m_AppTitle);
  ImGui::Separator();

  Vec3 cp = m_Camera.GetPosition();
  ImGui::Text("Camera: (%.1f, %.1f, %.1f)", cp.x, cp.y, cp.z);
  ImGui::Text("Frame: %u | Objects: %zu", m_FrameCount, m_SceneObjects.size());
  ImGui::Text("F1:Grid F2:Colliders F10:Pause F11:Step F3:DebugTools");
  ImGui::Separator();

  auto *ctx = m_Window->GetContext();
  if (ctx) {
    auto *ld = ctx->GetLightingDebugData();
    auto *gd = ctx->GetGeometryDebugData();
    auto *ht = ctx->GetHelperTogglesData();
    ImGui::TextColored(ImVec4(0.5f, 1.f, 0.5f, 1), "Live Debug Data:");
    if (ld) {
      ImGui::Text("  Lights: %u | Shadows: %s | SSAO: %s", ld->lightCount,
                  ld->shadowsEnabled ? "ON" : "OFF",
                  ld->ssaoEnabled ? "ON" : "OFF");
    }
    if (gd) {
      ImGui::Text("  Objects: %u visible / %u total", gd->visibleObjects,
                  gd->totalObjects);
      ImGui::Text("  Scene Bounds: (%.0f, %.0f, %.0f)-(%.0f,%.0f,%.0f)",
                  gd->sceneBounds.min.x, gd->sceneBounds.min.y,
                  gd->sceneBounds.min.z, gd->sceneBounds.max.x,
                  gd->sceneBounds.max.y, gd->sceneBounds.max.z);
    }
    if (ht) {
      ImGui::Text("  Grid: %s | Col: %s | Pause: %s",
                  ht->showGrid ? "ON" : "OFF", ht->showColliders ? "ON" : "OFF",
                  ht->pauseRendering ? "PAUSED" : "RUNNING");
    }
  }
  ImGui::End();
}

void _3DTestApp::SyncDebugLights() {
    for (uint32 i = 0; i < m_DebugLights.size() && i < m_MeshRenderer->GetLightCount(); ++i) {
        auto& dl = m_DebugLights[i];
        if (!dl.dirty) continue;

        if (i == 0) {
            m_MeshRenderer->SetLightPosition(dl.position);
            m_MeshRenderer->SetLightColor(dl.color);
            m_MeshRenderer->SetLightIntensity(dl.intensity);
        } else {
            m_MeshRenderer->ClearLights();
            for (uint32 j = 0; j < m_DebugLights.size(); ++j) {
                MeshRenderer::Light l;
                l.position  = m_DebugLights[j].position;
                l.color     = m_DebugLights[j].color;
                l.intensity = m_DebugLights[j].intensity;
                m_MeshRenderer->AddLight(l);
            }
        }
        dl.dirty = false;
    }
}

void _3DTestApp::Run() {
  using namespace std::chrono;
  auto last = high_resolution_clock::now();

  InitUI();

  auto* glfwWin = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());

  glfwShowWindow(glfwWin);
  glfwFocusWindow(glfwWin);

  int initFbW, initFbH;
  glfwGetFramebufferSize(glfwWin, &initFbW, &initFbH);
  if (initFbW > 0 && initFbH > 0) {
      OnWindowResize(initFbW, initFbH);
  }

  m_MenuManager.SetStartGameCallback([this]() {
    m_MenuManager.isVisible = false;
    m_MenuManager.currentPage = MenuManager::Page::None;
    m_GameStarted = true;
    m_PerfWindow.SetVisible(true);
  });
  m_MenuManager.SetQuitCallback([this]() {
    glfwSetWindowShouldClose(
        static_cast<GLFWwindow *>(m_Window->GetNativeHandle()), GLFW_TRUE);
  });

  m_MenuManager.isVisible = true;
  m_MenuManager.currentPage = MenuManager::Page::MainMenu;
  m_PerfWindow.SetVisible(false);

  while (!m_Window->ShouldClose()) {
    auto now = high_resolution_clock::now();
    float dt = std::min(duration<float>(now - last).count(), 0.05f);
    last = now;

    m_Window->PollEvents();
    HandleInput(dt);
    MemoryTracker::FrameStart();

    int w, h;
    glfwGetFramebufferSize(glfwWin, &w, &h);
    bool isMinimized = (w == 0 || h == 0);
    static int lw = 0, lh = 0;
    if (w != lw || h != lh) {
      lw = w; lh = h;
      if (!isMinimized) OnWindowResize(w, h);
    }

    ++m_FrameCount;

    auto *ctx = m_Window->GetContext();
    if (!ctx || isMinimized) {
      MemoryTracker::FrameEnd();
      m_InputManager.OnUpdate();
      continue;
    }

    // ── 更新阶段 ──
    bool canRender3D = m_GameStarted;
    m_MenuManager.OnUpdate(dt);
    UpdateLogic(dt);

    if (m_AnimateLights && canRender3D) {
      m_LightAngle += dt * 15.0f;
      float r = m_LightAngle * 3.14159f / 180.0f;
      m_MeshRenderer->SetLightPosition(Vec3(std::cos(r) * 20, 18, std::sin(r) * 20));
    }

    ctx->ClearColor(0.02f, 0.02f, 0.02f, 1.0f);

    // ═══ 3D 渲染管线 ═══
    // Shadow Pass（m_ShadowMapper 为 nullptr 时跳过阴影渲染）
    {
      int32 q = ctx->BeginGPUPass("ShadowPass");
      if (canRender3D && m_ShadowMapper) {
        m_ShadowMapper->BindForShadowPass();
        if (m_DepthShader) m_DepthShader->Bind();
        m_MeshRenderer->RenderWithSceneGraph(m_SceneObjects);
        m_ShadowMapper->EndShadowPass();
      }
      ctx->EndGPUPass(q);
    }

    // Base Pass
    {
      int32 q = ctx->BeginGPUPass("BasePass");
      if (canRender3D) {
        if (m_3DShader) {
          m_3DShader->Bind();
          ctx->BindViewModeUniform(m_3DShader.get());
        }
        m_MeshRenderer->RenderWithSceneGraph(m_SceneObjects);
      }
      RenderHelperVisualizations();
      ctx->EndGPUPass(q);
    }

    // PostProcess Pass
    {
      int32 q = ctx->BeginGPUPass("PostProcess");
      ctx->EndGPUPass(q);
    }

    // Resolve
    if (canRender3D) {
      static_cast<OpenGLContext *>(ctx)->ResolveToDefault();
    }

    // 清除 Backbuffer 深度缓冲
    {
      static_cast<OpenGLContext*>(ctx)->GetGL().Clear(GL_DEPTH_BUFFER_BIT);
    }

    // ═══ UI 渲染管线 ═══
    {
      int32 q = ctx->BeginGPUPass("UIPass");

      auto* glCtx = static_cast<OpenGLContext*>(ctx);
      glCtx->ResetPipelineState();

      auto& gl = glCtx->GetGL();
      gl.Disable(GL_DEPTH_TEST);
      gl.DepthMask(GL_FALSE);
      gl.Disable(GL_CULL_FACE);
      gl.Enable(GL_BLEND);
      gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      gl.Enable(GL_SCISSOR_TEST);

      SyncDebugLights();

      if (m_UIInitialized && UIManager::Get()) {
        PopulateLightingDebugData();
        PopulateGeometryDebugData();
        PopulatePostProcessingDebugData();
        PopulateTextureDebugData();

        uint32 dc = ctx->GetAndResetDrawCallCount(),
               vc = ctx->GetAndResetVertexCount(),
               tc = ctx->GetAndResetTriangleCount();
        m_PerfWindow.FeedStats(dt * 1000, dc, (uint32)m_SceneObjects.size(), 0, 0, 0);
        m_PerfWindow.SetGeometryCount(vc, tc);
        m_PerfWindow.SetCPUTime(dt * 1000);
        {
          GPUProfileFrame gf;
          if (ctx->GetGPUProfileFrame(gf)) {
            m_PerfWindow.SetGPUTime(gf.GetTotalMs());
            GPUProfilerSnapshot s;
            s.passCount = std::min(gf.passCount, (uint32)GPUProfilerSnapshot::kMaxPasses);
            for (uint32 i = 0; i < s.passCount; ++i) {
              s.passes[i].name = gf.passes[i].passName;
              s.passes[i].timeMs = gf.passes[i].elapsedMs;
            }
            m_PerfWindow.SetGPUProfiler(s);
          }
          m_PerfWindow.SetVRAMUsage(ctx->GetTextureVRAMBytes(), ctx->GetBufferVRAMBytes(), 0);
        }

        {
          auto *pp = ctx->GetPostProcessingDebugData();
          if (pp && glCtx) {
            AntiAliasingConfig aaCfg = {};
            if (pp->antiAliasingEnabled && pp->aaEnable) {
              switch (pp->aaType) {
              case AAType::MSAA: aaCfg.mode = AntiAliasingMode::MSAA; aaCfg.sampleCount = 4; break;
              case AAType::FXAA: aaCfg.mode = AntiAliasingMode::MLAA; break;
              case AAType::TAA:  aaCfg.mode = AntiAliasingMode::MLAA; break;
              case AAType::SMAA: aaCfg.mode = AntiAliasingMode::MLAA; break;
              default: aaCfg.mode = AntiAliasingMode::None; break;
              }
            } else {
              aaCfg.mode = AntiAliasingMode::None;
            }
            static AntiAliasingConfig s_LastAACfg = {};
            if (aaCfg.mode != s_LastAACfg.mode || aaCfg.sampleCount != s_LastAACfg.sampleCount) {
              glCtx->SetAntiAliasingConfig(aaCfg);
              s_LastAACfg = aaCfg;
            }
          }
        }

        UIManager::Begin();
        m_PerfWindow.OnImGui();
        DrawDebugImGui();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(10, io.DisplaySize.y - 320), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 20, 310), ImGuiCond_Always);
        m_ConsolePanel.OnImGui();

        if (m_MenuManager.isVisible)
          m_MenuManager.OnImGui();
        UIManager::End();
      }
      ctx->EndGPUPass(q);
    }

    MemoryTracker::FrameEnd();
    Input::SetBlockInput(false, false);
    m_InputManager.OnUpdate();

    ctx->SwapBuffers();
  }
}

void _3DTestApp::PopulateLightingDebugData() {
  auto *ctx = m_Window->GetContext();
  if (!ctx) return;
  auto *ld = ctx->GetLightingDebugData();
  if (!ld) return;

  ld->lightCount = (uint32)std::min((size_t)m_DebugLights.size(),
                                    (size_t)LightingDebugFrameData::kMaxLights);
  static char lightNames[LightingDebugFrameData::kMaxLights][32];

  for (uint32 i = 0; i < ld->lightCount; ++i) {
    auto &dst = ld->lights[i];
    auto& src = m_DebugLights[i];

    snprintf(lightNames[i], sizeof(lightNames[i]), "Light_%u", i);
    dst.name = lightNames[i];
    dst.position  = src.position;
    dst.color     = src.color;
    dst.intensity = src.intensity;
    dst.enabled   = true;
    dst.isShadowCaster = (i == 0);

    const auto& rl = m_MeshRenderer->GetLight(i);
    if (src.position.x != rl.position.x || src.position.y != rl.position.y || src.position.z != rl.position.z ||
        src.color.x != rl.color.x || src.color.y != rl.color.y || src.color.z != rl.color.z ||
        src.intensity != rl.intensity) {
        src.position  = dst.position;
        src.color     = dst.color;
        src.intensity = dst.intensity;
        src.dirty     = true;
    }
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
    ld->shadowTexWidth = m_ShadowMapper->GetShadowMapSize();
    ld->shadowTexHeight = m_ShadowMapper->GetShadowMapSize();
  }
}

void _3DTestApp::PopulateGeometryDebugData() {
  auto *ctx = m_Window->GetContext();
  if (!ctx) return;
  auto *gd = ctx->GetGeometryDebugData();
  if (!gd) return;
  gd->totalObjects = (uint32)m_SceneObjects.size();
  gd->visibleObjects = gd->totalObjects;
  gd->culledObjects = 0;
  AABB sb;
  sb.Expand(Vec3(-20, -5, -20));
  sb.Expand(Vec3(20, 15, 20));
  gd->sceneBounds = sb;
  gd->lod.currentLODCount = 3;
}

void _3DTestApp::PopulatePostProcessingDebugData() {
  auto *ctx = m_Window->GetContext();
  if (!ctx) return;
  auto *pp = ctx->GetPostProcessingDebugData();
  if (!pp) return;
  static bool initialized = false;
  if (!initialized) {
    pp->postProcessingEnabled = true;
    pp->bloomEnabled = true;
    pp->bloomIntensity = 1.2f;
    pp->bloomThreshold = 1.f;
    pp->colorGradingEnabled = true;
    pp->colorGradingStrength = 0.8f;
    pp->brightness = 1.f;
    pp->contrast = 1.1f;
    pp->saturation = 1.05f;
    pp->antiAliasingEnabled = true;
    pp->aaType = AAType::FXAA;
    pp->toneMappingEnabled = true;
    pp->toneMapMode = ToneMappingMode::ACES;
    pp->exposure = 1.f;
    pp->gamma = 2.2f;
    initialized = true;
  }
}

void _3DTestApp::PopulateTextureDebugData() {
  auto *ctx = m_Window->GetContext();
  if (!ctx) return;
  auto *td = ctx->GetTextureDebugData();
  if (!td) return;
  const char *tn[] = {"test.png", "brick_diffuse", "metal_roughness",
                      "skybox",   "normal_map",    "shadow_map",
                      "checker"};
  uint32 tw[] = {512, 1024, 512, 2048, 256, 2048, 128},
         th[] = {512, 1024, 512, 2048, 256, 2048, 128},
         tm[] = {10, 11, 10, 12, 9, 1, 7};
  uint64 tv[] = {1048576, 16777216, 4194304, 33554432, 262144, 16777216, 65536};
  td->textureCount = 7;
  td->totalVRAM = 0;
  for (uint32 i = 0; i < 7; ++i) {
    auto &t = td->textures[i];
    t.name = tn[i];
    t.width = tw[i];
    t.height = th[i];
    t.mipCount = tm[i];
    t.vramBytes = tv[i];
    td->totalVRAM += tv[i];
    t.isStreaming = (i >= 3 && i <= 5);
    t.isLoading = (i == 4);
    t.streamProgress = (i == 4) ? 0.65f : 1.f;
    t.isResident = !t.isLoading;
  }
}

void _3DTestApp::RenderHelperVisualizations() {
  auto *ctx = m_Window->GetContext();
  if (!ctx) return;
  auto *ht = ctx->GetHelperTogglesData();
  if (!ht) return;
  if (ht->showGrid) DrawGrid(ht->gridSize, ht->gridSteps);
  if (ht->showOriginAxis) DrawOriginAxis();
  if (ht->showColliders) {
    auto *b = m_MeshRenderer->GetBatch();
    if (b) {
      b->Begin(PrimitiveType::Lines);
      Vec3 c[8] = {{-2, -2, -2}, {2, -2, -2}, {2, -2, 2}, {-2, -2, 2},
                   {-2, 2, -2},  {2, 2, -2},  {2, 2, 2},  {-2, 2, 2}};
      int e[24] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6,
                   6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};
      Vec4 w(ht->colliderColor[0], ht->colliderColor[1], ht->colliderColor[2], 1.f);
      for (int i = 0; i < 24; i += 2) b->Line(c[e[i]], c[e[i + 1]], w);
      b->End();
    }
  }
}

void _3DTestApp::DrawGrid(float size, int steps) {
  auto *b = m_MeshRenderer->GetBatch();
  if (!b) return;
  b->Begin(PrimitiveType::Lines);
  float h = size * .5f, st = size / steps;
  Vec4 gc(.3f, .3f, .3f, .6f), ax(1, .2f, .2f, .8f), az(.2f, .2f, 1, .8f);
  for (int i = 0; i <= steps; ++i) {
    float t = -h + i * st;
    b->Line(Vec3(-h, 0, t), Vec3(h, 0, t), (i == steps / 2) ? ax : gc);
    b->Line(Vec3(t, 0, -h), Vec3(t, 0, h), (i == steps / 2) ? az : gc);
  }
  b->End();
}

void _3DTestApp::DrawOriginAxis() {
  auto *b = m_MeshRenderer->GetBatch();
  if (!b) return;
  b->Begin(PrimitiveType::Lines);
  b->Line(Vec3(0, 0, 0), Vec3(3, 0, 0), Vec4(1, 0, 0, 1));
  b->Line(Vec3(0, 0, 0), Vec3(0, 3, 0), Vec4(0, 1, 0, 1));
  b->Line(Vec3(0, 0, 0), Vec3(0, 0, 3), Vec4(0, 0, 1, 1));
  b->End();
}

void _3DTestApp::OnWindowResize(int w, int h) {
  m_Camera.SetAspectRatio((float)w / h);
  if (auto *c = m_Window->GetContext()) c->OnResize(w, h);
}

// ═══════════════════════════════════════════════════════════════
// 新渲染管线调试面板
// ═══════════════════════════════════════════════════════════════

bool _3DTestApp::InitDeferredRendering() {
    // 加载延迟渲染着色器
    m_DeferredGeomShader = m_Factory.CreateShader(
        "assets/shaders/deferred_geom.vert",
        "assets/shaders/deferred_geom.frag");
    if (!m_DeferredGeomShader) {
        Log::Warn("[3DTest] Deferred geom shader not loaded, falling back to forward");
        m_DeferredInitialized = false;
        return false;
    }

    m_DeferredLightShader = m_Factory.CreateShader(
        "assets/shaders/fullscreen_quad.vert",
        "assets/shaders/deferred_light.frag");
    if (!m_DeferredLightShader) {
        Log::Warn("[3DTest] Deferred light shader not loaded");
        m_DeferredInitialized = false;
        return false;
    }

    // 全屏四边形 shader（用于调试可视化）
    m_FullscreenQuadShader = m_Factory.CreateShader(
        "assets/shaders/fullscreen_quad.vert",
        "assets/shaders/deferred_light.frag");

    Log::Info("[3DTest] Deferred rendering shaders loaded");
    m_DeferredInitialized = true;
    return true;
}

void _3DTestApp::DrawRenderPipelineDebugPanel() {
    if (!m_UIInitialized) return;
    
    ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Render Pipeline Debug", nullptr);

    // 渲染模式选择
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1), "Rendering Mode");
    const char* modes[] = { "Forward Lit", "Deferred PBR", "GBuffer Albedo",
                            "GBuffer Normal", "GBuffer Roughness", "Depth Only" };
    int currentMode = (int)m_RenderMode;
    ImGui::Combo("Mode", &currentMode, modes, 6);
    m_RenderMode = (RenderMode)currentMode;

    ImGui::Separator();

    // RenderGraph / 传统模式切换
    ImGui::Checkbox("Use RenderGraph", &m_UseRenderGraph);
    ImGui::SameLine();
    ImGui::TextDisabled("(Experimental)");

    // 场景信息
    ImGui::Separator();
    ImGui::Text("Scene Objects: %zu", m_SceneObjects.size());
    ImGui::Text("Lights: %zu", m_DebugLights.size());
    ImGui::Text("Camera: (%.1f, %.1f, %.1f)", 
                m_Camera.GetPosition().x,
                m_Camera.GetPosition().y,
                m_Camera.GetPosition().z);

    ImGui::End();
}

void _3DTestApp::DrawPBRDebugPanel() {
    ImGui::SetNextWindowPos(ImVec2(400, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
    ImGui::Begin("PBR Debug", nullptr);

    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1), "Material Parameters");
    ImGui::SliderFloat("Roughness", &m_PBRDebug.roughness, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Metallic", &m_PBRDebug.metallic, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("AO", &m_PBRDebug.ao, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Normal Strength", &m_PBRDebug.normalStrength, 0.0f, 2.0f, "%.2f");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1), "Tone Mapping");
    ImGui::SliderFloat("Exposure", &m_PBRDebug.exposure, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("Gamma", &m_PBRDebug.gamma, 1.0f, 3.0f, "%.2f");

    // BRDF 可视化（仅在延迟模式下有用）
    ImGui::Separator();
    ImGui::Checkbox("Show BRDF Debug Overlay", &m_PBRDebug.showBRDF);

    // 显示 PBR 公式参考
    if (ImGui::CollapsingHeader("PBR Formula Reference")) {
        ImGui::TextWrapped("Cook-Torrance BRDF:");
        ImGui::TextWrapped("  f(l,v) = (kD * albedo/pi + D*G*F/(4*NdotV*NdotL)) * NdotL");
        ImGui::TextWrapped("  D: GGX NDF");
        ImGui::TextWrapped("  G: Smith-Schlick");
        ImGui::TextWrapped("  F: Schlick Fresnel");
    }

    ImGui::End();
}

void _3DTestApp::DrawPostProcessDebugPanel() {
    ImGui::SetNextWindowPos(ImVec2(400, 270), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
    ImGui::Begin("Post-Process Debug", nullptr);

    ImGui::Checkbox("Enable Post-Processing", &m_PostDebug.enabled);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1), "Bloom");
    ImGui::Checkbox("Bloom", &m_PostDebug.bloomEnabled);
    ImGui::SliderFloat("Intensity", &m_PostDebug.bloomIntensity, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Threshold", &m_PostDebug.bloomThreshold, 0.0f, 3.0f, "%.2f");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1), "Anti-Aliasing");
    ImGui::Checkbox("TAA", &m_PostDebug.taaEnabled);
    if (m_PostDebug.taaEnabled) {
        ImGui::SliderFloat("TAA Blend", &m_PostDebug.taaBlendFactor, 0.0f, 1.0f, "%.3f");
    }
    ImGui::Checkbox("FXAA", &m_PostDebug.fxaaEnabled);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1), "Tone Mapping");
    const char* tmModes[] = { "None", "Reinhard", "ACES", "Unreal", "Filmic" };
    ImGui::Combo("Mode##TM", &m_PostDebug.toneMapMode, tmModes, 5);
    ImGui::SliderFloat("Exposure##TM", &m_PostDebug.exposure, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("Gamma##TM", &m_PostDebug.gamma, 1.0f, 3.0f, "%.2f");

    ImGui::Separator();
    ImGui::Checkbox("Show Debug Overlay", &m_PostDebug.showDebugOverlay);

    ImGui::End();
}

void _3DTestApp::DrawCSMDebugPanel() {
    ImGui::SetNextWindowPos(ImVec2(400, 530), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("CSM Shadow Debug", nullptr);

    ImGui::Checkbox("Enable Cascaded Shadows", &m_CSMDebug.enabled);
    ImGui::Checkbox("Show Cascade Overlay", &m_CSMDebug.showCascades);

    ImGui::SliderInt("Cascade Count", &m_CSMDebug.cascadeCount, 1, 4);
    ImGui::SliderFloat("Split Lambda", &m_CSMDebug.splitLambda, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Shadow Bias", &m_CSMDebug.shadowBias, 0.0f, 0.05f, "%.5f");

    const char* sizes[] = { "512", "1024", "2048", "4096" };
    int currentSize = 0;
    switch (m_CSMDebug.shadowMapSize) {
        case 512:  currentSize = 0; break;
        case 1024: currentSize = 1; break;
        case 2048: currentSize = 2; break;
        case 4096: currentSize = 3; break;
    }
    if (ImGui::Combo("Shadow Map Size", &currentSize, sizes, 4)) {
        int vals[] = { 512, 1024, 2048, 4096 };
        m_CSMDebug.shadowMapSize = vals[currentSize];
    }

    // CSM 分割深度可视化
    if (ImGui::CollapsingHeader("Cascade Splits (View Z)")) {
        float nearZ = 0.1f, farZ = 200.0f;
        for (int i = 0; i < m_CSMDebug.cascadeCount; ++i) {
            float p = (float)(i + 1) / (float)m_CSMDebug.cascadeCount;
            float logSplit = nearZ * std::pow(farZ / nearZ, p);
            float unifSplit = nearZ + (farZ - nearZ) * p;
            float split = logSplit * m_CSMDebug.splitLambda + unifSplit * (1.0f - m_CSMDebug.splitLambda);
            ImGui::Text("Cascade %d: near=%.1f, far=%.1f", i,
                        i == 0 ? nearZ : split,
                        (i == m_CSMDebug.cascadeCount - 1) ? farZ : split);
        }
    }

    ImGui::End();
}

void _3DTestApp::RenderDeferredTest() {
    if (m_RenderMode == RenderMode::ForwardLit) return;
    if (!m_DeferredInitialized) return;

    auto *ctx = m_Window->GetContext();
    if (!ctx) return;
    auto* glCtx = static_cast<OpenGLContext*>(ctx);
    auto& gl = glCtx->GetGL();

    int w, h;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_Window->GetNativeHandle()), &w, &h);

    // ── 1. 创建/重置 GBuffer ──
    if (!m_GBufferCreated || !m_DeferredGBuffer) {
        m_DeferredGBuffer = CreateGBuffer(*ctx);
        GBufferConfig cfg;
        cfg.width = (uint32)w;
        cfg.height = (uint32)h;
        if (m_DeferredGBuffer->Initialize(cfg)) {
            m_GBufferCreated = true;
            Log::Info("[3DTest] GBuffer created ({}x{})", w, h);
        } else {
            Log::Error("[3DTest] GBuffer creation failed");
            return;
        }
    }

    // ── 2. GBuffer Pass: 渲染场景几何体到 G-Buffer ──
    {
        int32 gq = ctx->BeginGPUPass("GBufferPass");
        glCtx->ResetPipelineState();

        m_DeferredGBuffer->BindForGeometryPass();
        gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl.Enable(GL_DEPTH_TEST);
        gl.DepthMask(GL_TRUE);

        if (m_DeferredGeomShader) {
            m_DeferredGeomShader->Bind();

            // 设置相机矩阵
            const Mat4& viewMat = m_Camera.GetViewMatrix();
            const Mat4& projMat = m_Camera.GetProjectionMatrix();

            m_DeferredGeomShader->SetMat4("u_View", viewMat.Data());
            m_DeferredGeomShader->SetMat4("u_Projection", projMat.Data());
            m_DeferredGeomShader->SetVec4("u_ObjectColor", Vec4(1, 1, 1, 1).Data());
            m_DeferredGeomShader->SetFloat("u_Roughness", m_PBRDebug.roughness);
            m_DeferredGeomShader->SetFloat("u_Metallic", m_PBRDebug.metallic);
            m_DeferredGeomShader->SetFloat("u_AO", m_PBRDebug.ao);
            m_DeferredGeomShader->SetFloat("u_NormalStrength", m_PBRDebug.normalStrength);
            m_DeferredGeomShader->SetInt("u_HasTexture", 0);
            m_DeferredGeomShader->SetInt("u_HasNormalMap", 0);

            // 渲染所有场景物体
            Vec3 camPos = m_Camera.GetPosition();
            m_DeferredGeomShader->SetVec3("u_EmissiveColor", Vec3(0, 0, 0).Data());

            for (auto* obj : m_SceneObjects) {
                if (!obj) continue;
                const auto& tf = obj->GetTransform();
                Mat4 modelMat = tf.GetWorldMatrix();
                auto* mc = obj->GetComponent<MeshComponent>();
                if (!mc || !mc->GetMesh()) continue;

                m_DeferredGeomShader->SetMat4("u_Model", modelMat.Data());
                m_DeferredGeomShader->SetVec4("u_ObjectColor", mc->m_Color.Data());

                // 法线矩阵 = transpose(inverse(model))
                Mat4 normalMat = modelMat;
                m_DeferredGeomShader->SetMat4("u_NormalMatrix", normalMat.Data());

                // 简单 MVP = proj * view * model
                Mat4 vp;
                Mat4Multiply(projMat, viewMat, vp);
                Mat4 mvp;
                Mat4Multiply(vp, modelMat, mvp);
                m_DeferredGeomShader->SetMat4("u_MVP", mvp.Data());

                mc->GetMesh()->Render();
            }
        }

        m_DeferredGBuffer->Unbind();
        ctx->EndGPUPass(gq);
    }

    // ── 3. Deferred Lighting Pass / GBuffer Debug Visualization ──
    {
        int32 lq = ctx->BeginGPUPass("LightingPass");

        glCtx->ResetPipelineState();
        gl.Disable(GL_DEPTH_TEST);
        gl.DepthMask(GL_FALSE);
        gl.Disable(GL_CULL_FACE);

        if (m_RenderMode == RenderMode::DeferredLit) {
            // Deferred Lighting: 全屏四边形 + PBR
            if (m_DeferredLightShader) {
                m_DeferredLightShader->Bind();
                m_DeferredGBuffer->BindTexturesForLighting();

                // 相机参数
                Vec3 camPos = m_Camera.GetPosition();
                m_DeferredLightShader->SetVec3("u_ViewPos", camPos.Data());
                m_DeferredLightShader->SetVec3("u_AmbientColor", m_MeshRenderer->GetAmbientColor().Data());
                m_DeferredLightShader->SetFloat("u_AmbientIntensity", 1.0f);
                m_DeferredLightShader->SetMat4("u_View", m_Camera.GetViewMatrix().Data());
                m_DeferredLightShader->SetMat4("u_Proj", m_Camera.GetProjectionMatrix().Data());

                // 光源数据（从 MeshRenderer 同步）
                uint32 lightCount = (uint32)m_DebugLights.size();
                for (uint32 i = 0; i < lightCount && i < 4; ++i) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "u_LightPos[%u]", i);
                    m_DeferredLightShader->SetVec3(buf, m_DebugLights[i].position.Data());
                    snprintf(buf, sizeof(buf), "u_LightColor[%u]", i);
                    m_DeferredLightShader->SetVec3(buf, m_DebugLights[i].color.Data());
                    snprintf(buf, sizeof(buf), "u_LightIntensity[%u]", i);
                    m_DeferredLightShader->SetFloat(buf, m_DebugLights[i].intensity);
                }
                m_DeferredLightShader->SetInt("u_LightCount", (int)lightCount);

                // 阴影禁用 (使用 SetInt 替代没有的 SetBool)
                m_DeferredLightShader->SetInt("u_ShadowEnabled", 0);

                // 绘制全屏四边形
                m_MeshRenderer->GetBatch()->Begin(PrimitiveType::Triangles);
                m_MeshRenderer->GetBatch()->Vertex(Vec3(-1, -1, 0), Vec3(0, 0, 1), Vec2(0, 0), Vec4(1, 1, 1, 1));
                m_MeshRenderer->GetBatch()->Vertex(Vec3(1, -1, 0), Vec3(0, 0, 1), Vec2(1, 0), Vec4(1, 1, 1, 1));
                m_MeshRenderer->GetBatch()->Vertex(Vec3(1, 1, 0), Vec3(0, 0, 1), Vec2(1, 1), Vec4(1, 1, 1, 1));
                m_MeshRenderer->GetBatch()->Vertex(Vec3(-1, 1, 0), Vec3(0, 0, 1), Vec2(0, 1), Vec4(1, 1, 1, 1));
                m_MeshRenderer->GetBatch()->Index(0); m_MeshRenderer->GetBatch()->Index(1); m_MeshRenderer->GetBatch()->Index(2);
                m_MeshRenderer->GetBatch()->Index(0); m_MeshRenderer->GetBatch()->Index(2); m_MeshRenderer->GetBatch()->Index(3);
                m_MeshRenderer->GetBatch()->Commit();
                m_MeshRenderer->GetBatch()->End();
            }
        } else {
            // GBuffer Debug Visualization: 直接将 GBuffer 纹理显示到屏幕
            // 使用全屏四边形 shader，手动绑定 GBuffer 纹理
            gl.ActiveTexture(GL_TEXTURE0);
            switch (m_RenderMode) {
                case RenderMode::GBufferAlbedo:
                    gl.BindTexture(GL_TEXTURE_2D, m_DeferredGBuffer->GetAlbedoTex());
                    break;
                case RenderMode::GBufferNormal:
                    gl.BindTexture(GL_TEXTURE_2D, m_DeferredGBuffer->GetNormalTex());
                    break;
                case RenderMode::GBufferRoughness:
                    gl.BindTexture(GL_TEXTURE_2D, m_DeferredGBuffer->GetPBRTex());
                    break;
                case RenderMode::DepthOnly:
                    gl.BindTexture(GL_TEXTURE_2D, m_DeferredGBuffer->GetDepthTex());
                    break;
                default:
                    break;
            }

            // GBuffer 调试可视化 — 使用 Batch 绘制全屏纹理
            if (m_FullscreenQuadShader) {
                m_FullscreenQuadShader->Bind();
                m_FullscreenQuadShader->SetInt("u_Texture", 0);

                // 使用 MeshRenderer 的 batch 绘制全屏四边形
                auto* batch = m_MeshRenderer->GetBatch();
                if (batch) {
                    batch->Begin(PrimitiveType::Triangles);
                    batch->Vertex(Vec3(-1, -1, 0), Vec3(0, 0, 1), Vec2(0, 0), Vec4(1, 1, 1, 1));
                    batch->Vertex(Vec3(1, -1, 0), Vec3(0, 0, 1), Vec2(1, 0), Vec4(1, 1, 1, 1));
                    batch->Vertex(Vec3(1, 1, 0), Vec3(0, 0, 1), Vec2(1, 1), Vec4(1, 1, 1, 1));
                    batch->Vertex(Vec3(-1, 1, 0), Vec3(0, 0, 1), Vec2(0, 1), Vec4(1, 1, 1, 1));
                    batch->Index(0); batch->Index(1); batch->Index(2);
                    batch->Index(0); batch->Index(2); batch->Index(3);
                    batch->Commit();
                    batch->End();
                }
            }
        }

        ctx->EndGPUPass(lq);
    }
}

} // namespace Engine
