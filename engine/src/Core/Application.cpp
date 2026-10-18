#include "Engine/Application.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/IWindow.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Core/Resources/FileWatcher.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Platform/PlatformUtils.h"
#include "Engine/Core/Log.h"
#include "Engine/UIManager.h"
#include "Engine/Profiler.h"
#include "Engine/MemoryTracker.h"

// 内存泄露检测
#if defined(_DEBUG) && defined(_WIN32)
#include <crtdbg.h>
#endif
#include "Engine/ConsolePanel.h"
#include "Engine/DockspaceBuilder.h"
#include "Engine/Debug/CrashHandler.h"
#include "Engine/Debug/ScreenshotCapture.h"
#include "Engine/Editor/EditorCamera.h"
#include "Engine/Editor/ViewportPanel.h"
#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include "Engine/OpenGL/OpenGLContext.h"
// HRC-3 P1：production scene render（sprite batch 路径）。刻意**不**引入
// ViewportPanel / EngineEditor 的任何渲染依赖（P1-H1 / P1-H2）。
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/RHI/SceneRenderer.h"
#include "Engine/Core/RHI/RenderQueue.h"
#include "Engine/Core/RHI/RenderCommand.h"
#include "Engine/Core/Renderer/OrthographicCamera.h"
#include "Engine/Core/Renderer/SpriteBatch.h"
#include <thread>
#include <chrono>
#include <cmath>

namespace Engine {

// 静态成员定义
ConsolePanel* Application::s_ConsolePanel = nullptr;
Application* Application::s_Instance = nullptr;

// 供 CrashHandler 在崩溃时读取子系统分配器状态
StackAllocator* g_SubsystemAllocator = nullptr;

bool Application::IsSubsystemAllocatorPresent() {
  return g_SubsystemAllocator != nullptr;
}

Application::Application(IGraphicsFactory &factory, bool headless)
    : m_Factory(factory), m_TextureManager(factory), m_Headless(headless) {
  s_Instance = this;

  m_SubsystemManager.Add("Window", SubsystemPhase::Platform,
                         [this]() { return InitWindow(); });

  m_SubsystemManager.Add("Camera", SubsystemPhase::Graphics,
                         [this]() { return InitCamera(); });

  m_SubsystemManager.Add(
      "UI", SubsystemPhase::Graphics, [this]() { return InitUI(); },
      []() { UIManager::Shutdown(); });

  // ── 统一资源管理器（在窗口和图形上下文就绪后初始化） ──
  m_SubsystemManager.Add(
      "ResourceManager", SubsystemPhase::Resources,
      [this]() {
        if (m_Headless) return true;
        ResourceManager::Init(m_Factory);
        return ResourceManager::Get() != nullptr;
      },
      [this]() {
        if (!m_Headless) ResourceManager::Shutdown();
      });

  // ── 文件监视器（用于热加载） ──
  m_SubsystemManager.Add(
      "FileWatcher", SubsystemPhase::Resources,
      [this]() {
        if (m_Headless) return true;
        FileWatcher::Init();
        return FileWatcher::Get() != nullptr;
      },
      [this]() {
        if (!m_Headless) FileWatcher::Shutdown();
      });

  m_SubsystemManager.Add("Shader", SubsystemPhase::Resources,
                         [this]() { return InitShader(); });

  m_SubsystemManager.Add(
      "Texture", SubsystemPhase::Assets,
      [this]() {
        if (m_Headless) return true;
        m_Texture = m_TextureManager.Load("assets/textures/test.png");
        return m_Texture != nullptr;
      },
      [this]() { m_Texture.reset(); });

  m_SubsystemManager.Add("VertexData", SubsystemPhase::Assets,
                         [this]() { return InitVertexData(); });
}

Application::~Application() {
  if (m_Headless) {
    Shutdown();
  }
  if (s_Instance == this) s_Instance = nullptr;
  g_SubsystemAllocator = nullptr;
  CrashHandler::Shutdown();
}

bool Application::InitializeHeadless() {
  if (!m_Headless) return false;
  if (m_Initialized) return true;
  if (m_ShutdownComplete) return false;

  Log::Init();
  m_Factory.SetAllocator(&m_SubsystemAllocator);
  g_SubsystemAllocator = &m_SubsystemAllocator;
  CrashHandler::Init("crashes");

  if (!m_SubsystemManager.Initialize()) {
    Shutdown();
    return false;
  }
  Time::Init();
  OnStartup();
  m_Initialized = true;
  return true;
}

bool Application::PumpOneFrame(float32 dt) {
  if (!m_Headless || !m_Initialized || !std::isfinite(dt) || dt < 0.0f) {
    return false;
  }
  InternalUpdate(dt);
  return true;
}

void Application::Shutdown() {
  if (m_ShutdownComplete) return;
  if (m_Initialized) {
    m_SubsystemManager.Shutdown();
    Time::Shutdown();
    m_Initialized = false;
  }
  m_ShutdownComplete = true;
  m_Factory.SetAllocator(nullptr);
  if (s_Instance == this) s_Instance = nullptr;
  g_SubsystemAllocator = nullptr;
}

void Application::RegisterSubsystem(std::string name, SubsystemPhase phase,
                                    std::function<bool()> init,
                                    std::function<void()> shutdown) {
  m_SubsystemManager.Add(std::move(name), phase, std::move(init),
                         std::move(shutdown));
}

bool Application::InitWindow() {
  if (m_Headless) return true;
  m_Window = m_Factory.CreateWindow(800, 600, "Engine");
  if (!m_Window) {
    Log::Error("Failed to create window");
    return false;
  }
  Log::Info("Window created {}x{}", 800, 600);
  return true;
}

bool Application::InitCamera() {
  m_Camera = OrthographicCamera(-1.6f, 1.6f, -0.9f, 0.9f);
  return true;
}

bool Application::InitShader() {
  if (m_Headless) return true;
  m_Shader = m_Factory.CreateShader("assets/shaders/vertex.glsl",
                                    "assets/shaders/fragment.glsl");
  if (!m_Shader) {
    Log::Error("Failed to create shader");
    return false;
  }
  return true;
}

bool Application::InitVertexData() {
  if (m_Headless) return true;
  float vertices[] = {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.5f, -0.5f,
                      0.0f,  1.0f,  0.0f, 0.5f, 0.5f, 0.0f, 1.0f,
                      1.0f,  -0.5f, 0.5f, 0.0f, 0.0f, 1.0f};
  uint32 indices[] = {0, 1, 2, 2, 3, 0};

  auto vb = m_Factory.CreateVertexBuffer(vertices, sizeof(vertices));
  if (!vb) {
    Log::Error("Failed to create vertex buffer");
    return false;
  }

  auto ib =
      m_Factory.CreateIndexBuffer(indices, sizeof(indices) / sizeof(uint32));
  if (!ib) {
    Log::Error("Failed to create index buffer");
    return false;
  }

  m_VAO = m_Factory.CreateVertexArray();
  m_VAO->AddVertexBuffer(vb);
  m_VAO->SetIndexBuffer(ib);

  Log::Info("Vertex data initialized (4 vertices, 6 indices)");
  return true;
}

bool Application::InitUI() {
  if (m_Headless) return true;
  auto ui = m_Factory.CreateUIManager();
  if (!ui) {
    Log::Error("Failed to create UI manager");
    return false;
  }
  bool ok = UIManager::Init(std::move(ui),
                             m_Window->GetNativeHandle(),
                             m_Window->GetContext());
  return ok && UIManager::IsInitialized();
}

// ============================================================
// 游戏播放状态控制
// ============================================================

void Application::SetPlaying(bool playing) {
  if (m_IsPlaying == playing) return;
  m_IsPlaying = playing;
  if (playing) {
    // 进入游戏运行模式 → 显示游戏主菜单
    Log::Info("Game mode: PLAYING — activating MenuManager");
    m_MenuManager.NavigateTo(MenuManager::Page::MainMenu);
  } else {
    // 回到编辑模式 → 隐藏游戏菜单
    Log::Info("Game mode: STOPPED — hiding MenuManager");
    m_MenuManager.currentPage = MenuManager::Page::None;
  }
}

// ============================================================
// 混合驱动 API
// ============================================================

uint32 Application::RegisterUpdateSubsystem(
    const std::string& name,
    std::function<void(float32)> updateFn,
    const SubsystemConfig& config) {

  // 检查名称冲突
  auto it = m_SubsystemNameMap.find(name);
  if (it != m_SubsystemNameMap.end()) {
    Log::Warn("Subsystem '{}' already registered (ID={}), returning existing ID",
              name, it->second);
    return it->second;
  }

  uint32 id = m_NextSubsystemId++;
  m_SubsystemNameMap[name] = id;

  SubsystemUpdateEntry entry;
  entry.id       = id;
  entry.name     = name;
  entry.updateFn = std::move(updateFn);
  entry.config   = config;
  // 运行时状态初始化为零
  entry.accumulator   = 0.0f;
  entry.timeSinceLast = 0.0f;
  entry.eventPending  = false;

  m_SubsystemEntries[id] = std::move(entry);

  Log::Info("RegisterUpdateSubsystem: '{}' (ID={}, mode={})",
            name, id, static_cast<int>(config.updateMode));
  return id;
}

void Application::UnregisterUpdateSubsystem(uint32 id) {
  auto it = m_SubsystemEntries.find(id);
  if (it == m_SubsystemEntries.end()) {
    Log::Warn("UnregisterUpdateSubsystem: ID={} not found.", id);
    return;
  }

  // 从名称映射中删除
  for (auto nIt = m_SubsystemNameMap.begin();
       nIt != m_SubsystemNameMap.end(); ++nIt) {
    if (nIt->second == id) {
      m_SubsystemNameMap.erase(nIt);
      break;
    }
  }

  Log::Info("UnregisterUpdateSubsystem: '{}' (ID={})", it->second.name, id);
  m_SubsystemEntries.erase(it);
}

void Application::MarkSubsystemDirty(uint32 id) {
  auto it = m_SubsystemEntries.find(id);
  if (it != m_SubsystemEntries.end()) {
    it->second.eventPending = true;
  }
}

// ============================================================
// 混合驱动调度器 — 按阶段并行 + 子系统独立策略
//
// 调度策略：
//   1. 按 SubsystemExecPhase 分阶段串行（PrePhysics → Physics
//      → PostPhysics → LateUpdate）
//   2. 同一阶段内的所有子系统通过 JobSystem 并行执行
//   3. 每个子系统内部的 UpdateMode（Fixed/Throttled 等）各自独立
//   4. 第一阶段开始前、最后阶段结束后各有一个全局同步点
// ============================================================

void Application::DispatchSubsystemUpdates(float32 dt) {
  PROFILE_ZONE();
  auto* js = JobSystem::Get();
  bool isActive = true;
  if (m_LoopMode == LoopMode::Adaptive && !m_Headless && m_Window) {
    isActive = m_Window->IsActive() && !m_Window->IsMinimized();
  }

  // ── 重入防护 ──
  if (m_InsideDispatch) return;
  m_InsideDispatch = true;

  // ── 按执行阶段分组 ──
  // 收集每个阶段需要更新的子系统
  using PhaseGroup = std::vector<uint32>;
  std::array<PhaseGroup, static_cast<size_t>(SubsystemExecPhase::COUNT)> phaseGroups;

  for (auto& [id, entry] : m_SubsystemEntries) {
    const auto& config = entry.config;

    // ── 后台模式过滤 ──
    if (!isActive && !config.runInBackground) {
      continue;
    }

    // ── EventDriven 过滤 ──
    if (config.updateMode == SubsystemUpdateMode::EventDriven && !entry.eventPending) {
      continue;
    }

    // ── Manual 跳过 ──
    if (config.updateMode == SubsystemUpdateMode::Manual) {
      continue;
    }

    // ── Throttled 限频判断 ──
    if (config.updateMode == SubsystemUpdateMode::Throttled) {
      entry.timeSinceLast += dt;
      float32 minInterval = 1.0f / config.maxHz;
      if (entry.timeSinceLast < minInterval) {
        continue;  // 未达到更新间隔
      }
      // 到达更新点，重置计时器（实际 dt 传递累计值）
    }

    uint8 phaseIdx = static_cast<uint8>(config.execPhase);
    if (phaseIdx < phaseGroups.size()) {
      phaseGroups[phaseIdx].push_back(id);
    }
  }

  // ── 按阶段顺序串行执行，每阶段内子系统并行 ──
  for (size_t phase = 0; phase < phaseGroups.size(); ++phase) {
    const auto& group = phaseGroups[phase];
    if (group.empty()) continue;

    // 同一阶段的子系统并行调度
    for (uint32 id : group) {
      auto it = m_SubsystemEntries.find(id);
      if (it == m_SubsystemEntries.end()) continue;

      auto& entry = it->second;
      const auto& config = entry.config;

      // 安全：值捕获 updateFn / config / 指针，避免在 Job 执行期间
      // 因 map 被修改而导致引用悬挂。
      auto* entryPtr   = &entry;
      auto  updateFn   = entry.updateFn;
      auto  updateMode = config.updateMode;
      auto  fixedDt    = config.fixedDt;

      auto jobFn = [entryPtr, updateFn, updateMode, fixedDt, dt]() {
        float32 actualDt = dt;

        switch (updateMode) {
          case SubsystemUpdateMode::Default:
          case SubsystemUpdateMode::Variable: {
            if (updateFn) updateFn(actualDt);
            break;
          }
          case SubsystemUpdateMode::Fixed: {
            entryPtr->accumulator += actualDt;
            while (entryPtr->accumulator >= fixedDt) {
              if (updateFn) updateFn(fixedDt);
              entryPtr->accumulator -= fixedDt;
            }
            break;
          }
          case SubsystemUpdateMode::Throttled: {
            if (updateFn) updateFn(entryPtr->timeSinceLast);
            entryPtr->timeSinceLast = 0.0f;
            break;
          }
          case SubsystemUpdateMode::EventDriven: {
            if (updateFn) updateFn(actualDt);
            entryPtr->eventPending = false;
            break;
          }
          default: break;
        }
      };

      // 通过 JobSystem 调度
      if (js && js->GetThreadCount() > 0) {
        JobFunc wrappedFn = [jobFn](uint32) { jobFn(); };
        JobHandle h = js->Schedule(std::move(wrappedFn));
        entry.currentJobId = h.id;
      } else {
        jobFn();
        entry.currentJobId = 0;
      }
    }

    // ── 等待此阶段所有并行 Job 完成 ──
    // 完成后，worker 线程对 entry 中 accumulator / timeSinceLast /
    // eventPending 的写入已通过 JobSystem 的 release-acquire 链对主线程可见。
    if (js && js->GetThreadCount() > 0) {
      for (uint32 id : group) {
        auto it = m_SubsystemEntries.find(id);
        if (it == m_SubsystemEntries.end()) continue;
        if (it->second.currentJobId != 0) {
          js->Wait(JobHandle{it->second.currentJobId});
          it->second.currentJobId = 0;
        }
      }
    }
  }

  m_InsideDispatch = false;
}

void Application::InternalUpdate(float32 dt) {
  PROFILE_ZONE();
  if (s_ConsolePanel) {
    // 使用直接的 Input 查询（绕过 s_BlockKeyboard，因为控制台切换必须始终响应）
    if (IInput* rawInput = Input::Get()) {
      if (rawInput->IsKeyPressed(KeyCode::GraveAccent)) {
        s_ConsolePanel->ToggleVisibility();
      }
    }

    // 控制台激活时阻止游戏输入
    bool consoleCaptures = s_ConsolePanel->IsCapturingInput();
    Input::SetBlockInput(consoleCaptures, consoleCaptures);
  }

  // ── 阶段 A：引擎级输入处理（串行） ──
  // 注意：如果控制台激活，Input::IsKeyDown 等会因 SetBlockInput 自动返回 false
  if (Input::IsKeyDown(KeyCode::W)) {
    // m_Camera->MoveForward(dt);
  }
  if (Input::IsKeyDown(KeyCode::A)) {
    // m_Camera->MoveLeft(dt);
  }

  float32 dx = Input::GetMouseDeltaX();
  float32 dy = Input::GetMouseDeltaY();
  // m_Camera->Rotate(dx, dy);

  // ── 阶段 B：子类扩展 ──
  OnUpdate(dt);

  // ── 阶段 C：混合驱动调度 ──
  // 所有通过 RegisterUpdateSubsystem 注册的子系统按各自配置更新
  // 内部会利用 JobSystem::ParallelFor 对可并行的任务进行调度
  DispatchSubsystemUpdates(dt);

  // ── 阶段 D：更新游戏菜单状态（仅在游戏运行时执行） ──
  if (m_IsPlaying) {
    m_MenuManager.OnUpdate(dt);

    // ── 转发按键到游戏菜单系统 ──
    if (m_MenuManager.isVisible && m_MenuManager.currentPage != MenuManager::Page::None) {
      if (Input::IsKeyPressed(KeyCode::Up))       m_MenuManager.OnKeyPressed(KeyCode::Up);
      if (Input::IsKeyPressed(KeyCode::Down))     m_MenuManager.OnKeyPressed(KeyCode::Down);
      if (Input::IsKeyPressed(KeyCode::Left))     m_MenuManager.OnKeyPressed(KeyCode::Left);
      if (Input::IsKeyPressed(KeyCode::Right))    m_MenuManager.OnKeyPressed(KeyCode::Right);
      if (Input::IsKeyPressed(KeyCode::Escape))   m_MenuManager.OnKeyPressed(KeyCode::Escape);
      if (Input::IsKeyPressed(KeyCode::Enter))    m_MenuManager.OnKeyPressed(KeyCode::Enter);
    }
  }

  // 注意：不在此处额外调用 WaitAll()。
  // DispatchSubsystemUpdates 已按阶段逐一等待完成，此处的显式同步是多余的，
  // 且在主线程 work-stealing 场景下可能导致不可预期的执行顺序。
}
void Application::InitViewportFBO() {
  auto* oglCtx = static_cast<OpenGLContext*>(m_Window->GetContext());
  if (!oglCtx) return;

  m_GL = &oglCtx->GetGL();

  // ── 性能优化：惰性分配 Application 的 FBO ──
  // EditorDemo 设置了 m_RenderDefaultQuad = false，完全不使用此 FBO
  // 仅在旧模式（m_RenderDefaultQuad = true）时才实际分配显存
  if (m_RenderDefaultQuad) {
    m_ViewportFBO = std::make_unique<OpenGLFramebuffer>(*m_GL);
    m_ViewportFBO->Resize(800, 600);
  }

  // ViewportPanel 现在自管理 MRT FBO，只需传递上下文和 GL 库指针
  m_ViewportPanel.SetRenderContext(m_Window->GetContext());
  m_ViewportPanel.SetGLContext(m_GL);
}

void Application::ResizeViewportFBO(int w, int h) {
  // ── 性能优化：惰性 resize，仅当 FBO 已分配时才重设 ──
  if (m_ViewportFBO) {
    m_ViewportFBO->Resize(static_cast<uint32>(w), static_cast<uint32>(h));
    m_EditorCamera.SetViewportSize(static_cast<float32>(w),
                                   static_cast<float32>(h));
  }
  // 将视口尺寸传递到 ViewportPanel 的 MRT FBO
  // ViewportPanel 内部已自管理 m_Width/m_Height 和 FBO 重建
}

void Application::InternalRender() {
  PROFILE_ZONE();
  if (m_Headless) return;

  // ── 性能优化：当 EngineEditor 接管渲染时跳过 Application 的冗余 FBO ──
  // ViewportPanel 已自管理 MRT FBO，所有场景渲染在 Render3DScene 中完成
  // 此处仅保留 OnRender() 供子类扩展，跳过 FBO 绑定/清除/ResetPipelineState
  // 以消除 ∼15-20ms 的 GPU 管线刷新开销

  auto* oglCtx = static_cast<OpenGLContext*>(m_Window->GetContext());
  if (!oglCtx) return;

  // ── 仅当启用默认四边形的旧模式时才绑定 Application 的 FBO ──
  if (m_RenderDefaultQuad) {
    if (m_ViewportFBO && m_ViewportFBO->IsValid()) {
      m_ViewportFBO->Bind();
    }

    GladGLContext& gl = oglCtx->GetGL();
    gl.ClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl.Enable(GL_DEPTH_TEST);

    if (m_Shader && m_VAO && m_Texture) {
      m_Shader->Bind();
      oglCtx->BindViewModeUniform(m_Shader.get());
      m_Shader->SetMat4("u_ViewProjection",
                        m_EditorCamera.GetViewProjectionMatrixPtr());
      m_Texture->Bind(0);
      m_VAO->Bind();
      oglCtx->DrawIndexed(m_VAO);
    }

    if (m_ViewportFBO && m_ViewportFBO->IsValid()) {
      m_ViewportFBO->Unbind();
    }
    oglCtx->ResetPipelineState();
  }

  // ── 子类扩展渲染（EditorDemo 中为空，不产生任何开销） ──
  OnRender();
}

// ================================================================
// HRC-3 P1（V1-a）：production scene render
//
// 目的：让 headless / production 路径**真的把场景画出来**，而不是只清屏 + 读一个探针像素。
// 所有权：渲染能力属于 Application（engine 侧），EngineHost 只负责编排（在既有 frame 内调用）。
// 明确边界（docs/Editor-Avalonia-HRC3-P1P2-Contract.md P1-H1~H7）：
//   - 不调用 ViewportPanel::OnUpdate，不依赖 legacy ViewportPanel / EngineEditor；
//   - 不引入 tick 源（由调用方在既有 frame 里驱动）；
//   - 不画 m_RenderDefaultQuad 之类的合成图案，只画 session 场景的真实 renderable；
//   - 没有可渲染内容 ⇒ 返回 false（**不允许 clear-only 成功**）；
//   - 任何失败 ⇒ 返回 false，由调用方转成 typed failure。
// ================================================================

bool Application::InitProductionRenderResources(IRenderContext &context) {
  if (m_ProductionRenderReady) {
    return true;
  }
  if (!m_Headless) {
    // v1 只服务 headless/production 路径；窗口化路径继续走既有 InternalRender()。
    return false;
  }

  // ── HRC-3 D3：production pick pass 必须拿到 GL context ──
  //
  // m_GL 此前**只**在 InitViewportFBO() 里赋值，而那条路径依赖非 headless 的
  // m_Window。EngineHost 用的是 headless Application（无窗口），因此 m_GL 会是
  // nullptr —— 结果 pick pass 静默跳过：不报错、也不写任何 ID。
  // 那种「看起来正常、其实什么都没做」的失败正是 D3-C1 要排除的，
  // 所以这里从传入的 render context 显式解析，而不是依赖窗口路径的副作用。
  if (auto *oglCtx = dynamic_cast<OpenGLContext *>(&context)) {
    m_GL = &oglCtx->GetGL();
  }
  if (!m_GL) {
    Log::Error("[HRC-3 D3] production render context is not an OpenGLContext; "
               "pick pass cannot run (it will NOT silently no-op)");
  }

  // sprite_batch.{vert,frag} 与 OpenGLSpriteBatch 的顶点布局一致
  // （location 0 = vec2 pos，1 = vec2 uv，2 = vec4 color）。刻意不用
  // vertex.glsl/fragment.glsl —— 那是 default-quad 的着色器，布局不匹配。
  m_ProductionSpriteShader = m_Factory.CreateShader(
      "assets/shaders/sprite_batch.vert", "assets/shaders/sprite_batch.frag");
  // HRC-3 P1（P1-H7）：`CreateShader` 只会返回**非空**包装对象，即使编译/链接失败
  // （`OpenGLShader` 在源文件读不到时只是 `SetState(Failed)` 且**不打印任何日志**）。
  // 因此必须显式检查资源状态，否则"着色器没建成"会退化成"渲染了但没有内容"。
  if (!m_ProductionSpriteShader) {
    Log::Error("[HRC-3 P1] sprite_batch shader object is null");
    return false;
  }
  if (m_ProductionSpriteShader->IsFailed() || !m_ProductionSpriteShader->IsLoaded()) {
    Log::Error("[HRC-3 P1] sprite_batch shader failed to build "
               "(vert=assets/shaders/sprite_batch.vert frag=assets/shaders/sprite_batch.frag)");
    m_ProductionSpriteShader.reset();
    return false;
  }

  m_ProductionSceneRenderer =
      std::make_shared<SceneRenderer>(m_Factory, m_TextureManager);
  m_ProductionSceneRenderer->SetRenderContext(context);
  m_ProductionSceneRenderer->SetShader(m_ProductionSpriteShader);
  m_ProductionCamera = std::make_unique<OrthographicCamera>(-1.0f, 1.0f, -1.0f, 1.0f);
  m_ProductionSceneRenderer->SetCamera(m_ProductionCamera.get());

  // ── HRC-3 D3：production pick pass 资源 ──
  // 专用 pick shader（U3）：只声明 location=1 的 int 输出，不与 deferred/normal
  // shader 共用 fragment-output contract（deferred_geom.frag 也占 location=1）。
  m_ProductionPickShader = m_Factory.CreateShader(
      "assets/shaders/pick.vert", "assets/shaders/pick.frag");
  if (!m_ProductionPickShader ||
      m_ProductionPickShader->IsFailed() || !m_ProductionPickShader->IsLoaded()) {
    // 刻意 **不** 让 pick 资源失败拖垮 color pass：P1 的颜色证据必须不受 D3 影响。
    // 但必须显式记录，否则会退化成"看起来正常、其实没写 ID"的静默失败。
    Log::Error("[HRC-3 D3] pick shader failed to build "
               "(vert=assets/shaders/pick.vert frag=assets/shaders/pick.frag)");
    m_ProductionPickShader.reset();
    m_ProductionPickReady = false;
  } else {
    m_ProductionPickSceneRenderer =
        std::make_shared<SceneRenderer>(m_Factory, m_TextureManager);
    m_ProductionPickSceneRenderer->SetRenderContext(context);
    m_ProductionPickSceneRenderer->SetShader(m_ProductionPickShader);
    // 共用**同一个** camera 指针：两个 pass 的 u_ViewProjection 必须完全一致。
    m_ProductionPickSceneRenderer->SetCamera(m_ProductionCamera.get());
    m_ProductionPickReady = true;
    Log::Info("[HRC-3 D3] production pick pass resources ready");
  }

  m_ProductionRenderReady = true;
  Log::Info("[HRC-3 P1] production sprite-batch render resources ready");
  return true;
}

bool Application::RenderProductionFrame(IRenderContext &context, Scene &scene,
                                        uint32_t targetWidth, uint32_t targetHeight) {
  m_ProductionRenderableCount = 0;

  if (!m_ProductionRenderReady && !InitProductionRenderResources(context)) {
    return false;   // typed failure：shader / sprite batch 创建失败
  }
  if (targetWidth == 0 || targetHeight == 0) {
    return false;
  }

  // ── 1. 收集真实 renderable，并按其世界坐标框定相机 ──
  // 相机必须覆盖 sprite 的世界坐标（SpriteComponent 的 x/y 来自 world matrix 平移），
  // 否则 quad 会落在视锥外，产出"看起来渲染了但整帧只有背景色"的假象。
  //
  // 用**自己的** RenderQueue 做统计/包围盒：SceneRenderer::GetRenderQueue() 返回
  // const 引用，且 Render() 内部会自己 Clear + Collect，这里不碰它的队列。
  RenderQueue probeQueue;
  scene.CollectRenderCommands(probeQueue);
  const auto &commands = probeQueue.GetCommands();
  if (commands.empty()) {
    // 「没有可渲染内容」**不是**运行时失败：运行态（例如 play 期间编辑场景被换出）下
    // 编辑场景可能合法地没有 renderable。把这种情况做成 frame 失败会破坏 HRC-2 冻结的
    // pump/frame 契约（曾经把 RuntimeSeamProofThreeCycles 打成 PumpOneFrame=-1）。
    //
    // 因此语义分层：
    //   - 生产：没有内容 ⇒ 什么都不画，返回成功，renderable 计数为 0；
    //   - 证据：gate 用 renderables > 0 作为前置断言，前置不成立就报
    //     "insufficient scene content"，绝不把 clear-only 帧当成通过（P1-H5）。
    m_ProductionRenderableCount = 0;
    return true;
  }

  float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
  for (size_t i = 0; i < commands.size(); ++i) {
    const auto &cmd = commands[i];
    const float x = cmd.worldMatrix[12];
    const float y = cmd.worldMatrix[13];
    const float halfSpriteW = 0.5f * std::sqrt(cmd.worldMatrix[0] * cmd.worldMatrix[0] +
                                                cmd.worldMatrix[1] * cmd.worldMatrix[1] +
                                                cmd.worldMatrix[2] * cmd.worldMatrix[2]);
    const float halfSpriteH = 0.5f * std::sqrt(cmd.worldMatrix[4] * cmd.worldMatrix[4] +
                                                cmd.worldMatrix[5] * cmd.worldMatrix[5] +
                                                cmd.worldMatrix[6] * cmd.worldMatrix[6]);
    if (i == 0) {
      minX = x - halfSpriteW; maxX = x + halfSpriteW;
      minY = y - halfSpriteH; maxY = y + halfSpriteH;
    } else {
      minX = std::min(minX, x - halfSpriteW); maxX = std::max(maxX, x + halfSpriteW);
      minY = std::min(minY, y - halfSpriteH); maxY = std::max(maxY, y + halfSpriteH);
    }
  }

  const float aspect = static_cast<float>(targetWidth) / static_cast<float>(targetHeight);
  const float cx = 0.5f * (minX + maxX);
  const float cy = 0.5f * (minY + maxY);
  float halfViewW = std::max(0.5f * (maxX - minX), 1.0f) * 1.10f;   // 10% 边距
  float halfViewH = std::max(0.5f * (maxY - minY), 1.0f) * 1.10f;
  // 保持目标宽高比：不足的一侧补齐，避免拉伸变形。
  if (halfViewW / halfViewH < aspect) halfViewW = halfViewH * aspect;
  else                               halfViewH = halfViewW / aspect;
  m_ProductionCamera = std::make_unique<OrthographicCamera>(
      cx - halfViewW, cx + halfViewW, cy - halfViewH, cy + halfViewH);
  m_ProductionSceneRenderer->SetCamera(m_ProductionCamera.get());

  // ── 2. 画进调用方已绑定的 framebuffer ──
  m_ProductionSpriteShader->Bind();
  // ISpriteBatch 需要绑定到 texture unit 0；其余按注释设置 uniform 数量 0，避免行数漂移
  m_ProductionSpriteShader->SetInt("u_Texture", 0);
  m_ProductionSceneRenderer->Render(scene);

  m_ProductionRenderableCount = static_cast<uint32_t>(commands.size());

  // ── HRC-3 D3：production pick pass（同一帧、同一批 command、同一 camera）──
  //
  // D3-C1：pick 真值必须是**当前** production frame 的 ID 表面，因此本 pass
  // 与上面的 color pass 在同一次 RenderProductionFrame 内完成，而不是"事后"
  // 对某个残留 buffer 采样（D3-C4 禁止"最近一次 pick buffer"语义）。
  //
  // 刻意复用 commands / ToSpriteData / camera / 同一个 sprite batch 实现：
  // 只要这里自己重算一份几何，ID 就会与颜色错位且**不会报错**。
  m_ProductionPickEntityCount = 0;
  // D3-C1/U2：pick pass 只在**显式启用**时运行，默认关闭。
  //
  // 关闭的理由是 contract，而不是任何挂起：U2 冻结的语义是「pick-ID pass **仅当存在
  // pending request** 时运行」，而当前**尚无 request channel**（那属于 D3 ABI
  // mini-contract）。没有请求却每帧跑 ID pass 纯属浪费。
  //
  // 注：D3-H1 已证明 pick pass 本身**不存在**已证实的 hang —— 连续三帧 P0..P8 全部
  // 完整执行。之前那次"挂起"来自 stale EditorBridge.dll，与 pick pass 无关。
  const bool m_ProductionPickEnabledForThisFrame = m_ProductionPickPassEnabled;
  if (m_ProductionPickEnabledForThisFrame && m_ProductionPickReady && m_GL &&
      m_ProductionPickSceneRenderer) {
    auto pickBatch = m_ProductionPickSceneRenderer->GetSpriteBatch();
    if (pickBatch) {

      // D3-C2：attachment 1 显式清成 sentinel **-1**。
      // 绝不能用普通 glClear —— 那会清成 0，而 0 是 reserved/invalid，
      // 未命中就会被读成假命中（false hit）。
      const GLint kNoHitSentinel = -1;
      const GLenum pickDrawBuffers[2] = {GL_NONE, GL_COLOR_ATTACHMENT1};
      const GLenum colorDrawBuffers[2] = {GL_COLOR_ATTACHMENT0, GL_NONE};
      m_GL->DrawBuffers(2, pickDrawBuffers);
      m_GL->ClearBufferiv(GL_COLOR, 1,
                          const_cast<GLint*>(&kNoHitSentinel));
      m_GL->Clear(GL_DEPTH_BUFFER_BIT);

      m_ProductionPickShader->Bind();
      m_ProductionPickShader->SetMat4(
          "u_ViewProjection",
          m_ProductionCamera->GetViewProjectionMatrixPtr());
      m_ProductionPickShader->SetInt("u_Texture", 0);

      // 逐 entity 提交：u_EntityID 是 per-draw uniform，所以每个 command
      // Draw + Flush 一次，保证 ID 与像素一一对应。
      std::shared_ptr<Texture> currentTexture;
      bool began = false;
      for (const auto &cmd : commands) {
        auto tex = cmd.texture;
        if (!tex) {
          continue;   // 与 color pass 相同的跳过规则，保持两 pass 像素一致
        }
        if (began && tex != currentTexture) {
          pickBatch->End();
          began = false;
        }
        if (!began) {
          currentTexture = tex;
          pickBatch->Begin(currentTexture);
          began = true;
        }
        if (cmd.entityId == 0) {
          // 0 = 未填充/无 owner。绝不写入 0（reserved），写 -1 保持 no-hit 语义。
          m_ProductionPickShader->SetInt("u_EntityID", kNoHitSentinel);
        } else {
          m_ProductionPickShader->SetInt(
              "u_EntityID", static_cast<int>(cmd.entityId));
        }
        SpriteData pickData;
        m_ProductionSceneRenderer->ToSpriteData(cmd, pickData);
        pickBatch->Draw(pickData);
        pickBatch->Flush();
        m_ProductionPickEntityCount += 1;
      }
      if (began) {
        pickBatch->End();
      }

      // 恢复 color attachment 为唯一输出，避免污染后续 pass。
      m_GL->DrawBuffers(2, colorDrawBuffers);
    }
  }

  return true;
}

void Application::Run() {

  // ── 内存泄露检测（Debug 模式，退出时自动报告） ──
#if defined(_DEBUG) && defined(_WIN32)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  // ── 初始化日志系统 ──
  Log::Init();

  // ── 将栈分配器注入工厂，所有子系统对象从连续内存池分配 ──
  m_Factory.SetAllocator(&m_SubsystemAllocator);
  g_SubsystemAllocator = &m_SubsystemAllocator;

  // ── 初始化崩溃报告系统 ──
  CrashHandler::Init("crashes");

  if (!m_SubsystemManager.Initialize()) {
    Log::Error("Subsystem initialization failed, aborting.");
    return;
  }

  // ── 初始化高精度计时器 ──
  Time::Init();

  // ── 初始化 Profiler（Tracy GPU 上下文 + 日志输出） ──
  // 必须在 JobSystem::Init() 之前调用！
  // Profiler 的初始化（特别是 TRACY_DELAYED_INIT）需要在主线程上完成校准。
  // 如果工作线程先调用 Tracy 宏（如 PROFILE_SET_THREAD_NAME），
  // 校准代码会在工作线程上执行并触发 std::system_error 崩溃。
  Profiler::Init();
  PROFILE_FRAME_START;

  // ── 初始化 Job 系统（线程池） ──
  // 传入 hardware_concurrency 个线程，预留 1 核给未来渲染线程
	JobSystem::Init(0);

  // ── 初始化场景管理器（工业级异步加载/切换/预加载） ──
  SceneManager::Init();

  OnStartup();

  // ── 初始化视口 FBO（在窗口和上下文就绪后） ──
  InitViewportFBO();

  // 设置 resize 回调
  m_ViewportPanel.SetResizeCallback([this](int32 w, int32 h) {
    ResizeViewportFBO(w, h);
  });

  // 设置编辑器相机初始视口尺寸
  m_EditorCamera.SetViewportSize(
      static_cast<float32>(m_ViewportPanel.GetWidth()),
      static_cast<float32>(m_ViewportPanel.GetHeight()));

  // ── 启动文件监视器（在资源加载完成后） ──
  if (auto *fw = FileWatcher::Get())
    fw->Start();
  while (!m_Window->ShouldClose()) {
    // ============================================================
    // 阶段 1：消息泵 + 窗口状态感知的帧率控制
    // ============================================================
    if (m_LoopMode == LoopMode::Adaptive) {
      // ── 自适应双模式 ──
      if (m_Window->IsActive() && !m_Window->IsMinimized()) {
        // 激活模式：非阻塞轮询，全速渲染
        m_Window->PollEvents();
      } else {
        // ── 后台模式：阻塞等待事件，零 CPU 占用 ──
        m_Window->WaitEvents(0.1);

        // 窗口最小化时完全跳过这一帧
        if (m_Window->IsMinimized()) {
          continue;
        }

        // ── 窗口正在被拖动调整大小 → 降帧到 ~30fps 保证窗口操作流畅 ──
        if (m_Window->IsResizing()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
      }
    } else {
      // ── 传统模式（Variable / Fixed）：不感知窗口状态 ──
      m_Window->PollEvents();
    }

    // ============================================================
    // 阶段 2：Delta Time（引擎统一维护，高精度 double）
    // ============================================================
    Time::UpdateDeltaTime();
    float32 dt = Time::GetDeltaTime();

    // ============================================================
    // 阶段 3：UI 帧开始（内含 NewFrame + 输入抢占）
    // ============================================================
    MemoryTracker::FrameStart();

    if (auto *ui = UIManager::Get())
      ui->Begin();

    // ── 阶段 3.3：更新场景管理器（异步加载流水线轮询、分帧激活、超时检测） ──
    SceneManager::Update(dt);

    // ============================================================
    // 阶段 3.5：ViewportPanel 已内部管理 EditorCamera 的输入和更新
    // 不再需要 Application 层的 m_EditorCamera（双相机冗余处理）
    // ============================================================
    // m_EditorCamera 保留仅用于旧模式 m_RenderDefaultQuad 的渲染矩阵
    // 让它的更新与 ViewportPanel 相机同步，避免额外开销
    if (m_RenderDefaultQuad) {
      m_EditorCamera.SetActive(m_ViewportPanel.IsHovered() ||
                               m_ViewportPanel.IsFocused());
      m_EditorCamera.ProcessInput(dt);
      m_EditorCamera.OnUpdate(dt);
    }

    // ============================================================
    // 阶段 4：更新（混合驱动 — 全局策略 + 子系统独立策略）
    // ============================================================
    if (m_LoopMode == LoopMode::Fixed) {
      // 固定步长模式 (Fixed 60Hz) 带漂移修正
      static const float32 fixedDt = 1.0f / 60.0f;
      static const int32   maxIter = 10;     // 防止螺旋过载
      static float64 acc = 0.0;

      // 每 600 帧（~10 秒）用绝对时间校准一次累加器，消除累积漂移
      static uint32 calibrateCounter = 0;
      if (++calibrateCounter >= 600) {
        acc = Time::CalibrateAccumulator(acc, fixedDt);
        calibrateCounter = 0;
      }

      acc += dt;
      int32 iterations = 0;
      while (acc >= fixedDt && iterations < maxIter) {
        InternalUpdate(fixedDt);
        acc -= fixedDt;
        ++iterations;
      }
      // 超过迭代上限 → 重置累加器，丢弃溢出时间以避免螺旋过载
      if (iterations >= maxIter) {
        acc = 0.0;
      }
      // 渲染插值因子（用于骨骼/动画视觉平滑）
      m_RenderAlpha = static_cast<float>(acc / fixedDt);  // 0~1
    } else {
      // Variable / Adaptive 模式都使用真实 dt
      InternalUpdate(dt);
      m_RenderAlpha = 1.0f;  // 无插值
    }

    // ============================================================
    // 阶段 5：每帧检测文件变更，触发热加载
    // ============================================================
    if (auto *rm = ResourceManager::Get())
      rm->PollHotReload();

    // ============================================================
    // 阶段 6：构建 UI（仅在可见时执行）
    // ============================================================
    if (auto *ui = UIManager::Get(); ui && ui->IsVisible()) {
      // ── 确保 PerformanceWindow 持有渲染上下文指针 ──
      auto* ctx = m_Window->GetContext();
      m_PerfWindow.SetRenderContext(ctx);

      uint32 drawCalls   = ctx->GetAndResetDrawCallCount();
      uint32 vertexCount = ctx->GetAndResetVertexCount();
      uint32 triCount    = ctx->GetAndResetTriangleCount();

      // 基础统计
      m_PerfWindow.FeedStats(dt * 1000.0f, drawCalls, 0, 0, 0, 0);

      // 几何体统计
      m_PerfWindow.SetGeometryCount(vertexCount, triCount);

      // CPU 耗时 (近似: 帧总时间 - GPU 时间)
      float32 cpuTimeMs = dt * 1000.0f;
      m_PerfWindow.SetCPUTime(cpuTimeMs);

      // GPU 耗时 (从时间戳查询)
      GPUProfileFrame gpuFrame;
      if (ctx->GetGPUProfileFrame(gpuFrame)) {
        m_PerfWindow.SetGPUTime(gpuFrame.GetTotalMs());

        // 填充 GPU Profiler 快照
        GPUProfilerSnapshot snap;
        snap.passCount = (std::min)(gpuFrame.passCount,
                                     static_cast<uint32>(GPUProfilerSnapshot::kMaxPasses));
        for (uint32 i = 0; i < snap.passCount; ++i) {
          snap.passes[i].name   = gpuFrame.passes[i].passName;
          snap.passes[i].timeMs = gpuFrame.passes[i].elapsedMs;
        }
        m_PerfWindow.SetGPUProfiler(snap);
      }

      // 显存占用
      m_PerfWindow.SetVRAMUsage(
        ctx->GetTextureVRAMBytes(),
        ctx->GetBufferVRAMBytes(),
        0 // model bytes — 暂未单独跟踪
      );

      // ── UI 容器布局 ──
      if (m_UseEngineEditorDockspace) {
        // ── EngineEditor 自管理布局 ──
        // 由 EngineEditor::OnImGui() 内部自行创建全屏 DockSpace 窗口
        //（含内嵌菜单栏），因此不在 Application 层包裹 DockspaceBuilder。
        //
        // 性能窗口仍由 Application 控制绘制，但其窗口会被 EngineEditor
        // 的 DockSpace 系统管理，用户可拖拽停靠。
        if (m_DrawPerformanceWindow)
          m_PerfWindow.OnImGui();

        // 子类自定义 UI（通常为 EngineEditor::OnImGui()）
        OnImGui();

        // ── 绘制内置游戏菜单（仅在游戏运行状态下渲染） ──
        if (m_IsPlaying && m_MenuManager.isVisible)
          m_MenuManager.OnImGui();
      } else {
        // ── 传统 Dockspace 工作台 ──
        // 所有编辑器面板被包裹在 DockspaceBuilder 中，形成 Unity/Unreal
        // 风格的可拖拽面板系统
        DockspaceBuilder dockspace;
        dockspace.Begin("MainDockspace");

        // 绘制性能监控窗口（可由子类通过 m_DrawPerformanceWindow=false
        // 交给 Editor 管理）
        if (m_DrawPerformanceWindow)
          m_PerfWindow.OnImGui();

        // 子类自定义 UI
        OnImGui();

        // ── 绘制内置游戏菜单（仅在游戏运行状态下渲染） ──
        if (m_IsPlaying && m_MenuManager.isVisible)
          m_MenuManager.OnImGui();

        dockspace.End();
      }
    }

    // ============================================================
    // 阶段 7：渲染
    // ============================================================
    InternalRender();

    // ── UI 帧结束 ──
    if (auto *ui = UIManager::Get())
      ui->End();

    // ============================================================
    // 阶段 8：预捕获截屏（供崩溃报告使用）
    // ============================================================
    {
      int32 sw = 0, sh = 0;
      std::vector<uint8_t> pixels;
      if (auto* ctx = m_Window->GetContext()) {
        if (ctx->CaptureFrameBuffer(sw, sh, pixels)) {
          Engine::ScreenshotCapture::CaptureFrame(sw, sh, pixels.data());
        }
      }
    }

    // ============================================================
    // 阶段 9：Swap Buffers + 帧尾消息泵
    // ============================================================
    m_Window->OnUpdate();

    // ── 阶段 9：Job 系统轮询（处理主线程回调） ──
    if (auto* js = JobSystem::Get())
      js->PollCompleted();

    // ── 帧标记（Tracy Profiler） ──
    PROFILE_FRAME("MainLoop");

    // ── 内存帧快照 ──
    MemoryTracker::FrameEnd();
  }

  // GP1-DX 启动调查（2026-08-26）：主循环退出点取证。若此前无
  // "WindowClose: OS-initiated" 告警，则 ShouldClose 由内部置位
  // （需排查 glfwSetWindowShouldClose 调用者）；有则来自 OS 关闭消息。
  {
    const double uptimeSec = Time::GetElapsedSinceInit();
    Log::Warn("Main loop exited after {:.1f}s (see preceding "
              "WindowClose warning for origin)", uptimeSec);
  }

  // ── 关闭场景管理器 ──
  SceneManager::Shutdown();

  // ── 先关闭 UI（必须在 SubsystemManager 析构前，因为后者会销毁窗口，
  //     而 ImGui 后端清理需要有效的 GLFW/OpenGL 上下文） ──
  UIManager::Shutdown();

  // ── Job 系统关闭 ──
  JobSystem::Shutdown();
}

} // namespace Engine
