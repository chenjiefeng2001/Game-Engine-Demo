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
#include "Engine/Debug/CrashHandler.h"
#include "Engine/Debug/ScreenshotCapture.h"
#include <thread>
#include <chrono>

namespace Engine {

// 静态成员定义
ConsolePanel* Application::s_ConsolePanel = nullptr;

// 供 CrashHandler 在崩溃时读取子系统分配器状态
StackAllocator* g_SubsystemAllocator = nullptr;

Application::Application(IGraphicsFactory &factory)
    : m_Factory(factory), m_TextureManager(factory) {

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
        ResourceManager::Init(m_Factory);
        return ResourceManager::Get() != nullptr;
      },
      []() { ResourceManager::Shutdown(); });

  // ── 文件监视器（用于热加载） ──
  m_SubsystemManager.Add(
      "FileWatcher", SubsystemPhase::Resources,
      []() {
        FileWatcher::Init();
        return FileWatcher::Get() != nullptr;
      },
      []() { FileWatcher::Shutdown(); });

  m_SubsystemManager.Add("Shader", SubsystemPhase::Resources,
                         [this]() { return InitShader(); });

  m_SubsystemManager.Add(
      "Texture", SubsystemPhase::Assets,
      [this]() {
        m_Texture = m_TextureManager.Load("assets/textures/test.png");
        return m_Texture != nullptr;
      },
      [this]() { m_Texture.reset(); });

  m_SubsystemManager.Add("VertexData", SubsystemPhase::Assets,
                         [this]() { return InitVertexData(); });
}

Application::~Application() {
  // SubsystemManager::Shutdown() 在 m_SubsystemManager 析构时自动调用

  CrashHandler::Shutdown();

  // ── 日志系统关闭（确保所有缓冲输出写入文件） ──
  Log::Shutdown();
}

void Application::RegisterSubsystem(std::string name, SubsystemPhase phase,
                                    std::function<bool()> init,
                                    std::function<void()> shutdown) {
  m_SubsystemManager.Add(std::move(name), phase, std::move(init),
                         std::move(shutdown));
}

bool Application::InitWindow() {
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
  m_Shader = m_Factory.CreateShader("assets/shaders/vertex.glsl",
                                    "assets/shaders/fragment.glsl");
  if (!m_Shader) {
    Log::Error("Failed to create shader");
    return false;
  }
  return true;
}

bool Application::InitVertexData() {
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
  if (m_LoopMode == LoopMode::Adaptive) {
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

  // ── 阶段 D：更新菜单状态（串行） ──
  m_MenuManager.OnUpdate(dt);

  // ── 转发按键到菜单系统 ──
  if (m_MenuManager.isVisible && m_MenuManager.currentPage != MenuManager::Page::None) {
    if (Input::IsKeyPressed(KeyCode::Up))       m_MenuManager.OnKeyPressed(KeyCode::Up);
    if (Input::IsKeyPressed(KeyCode::Down))     m_MenuManager.OnKeyPressed(KeyCode::Down);
    if (Input::IsKeyPressed(KeyCode::Left))     m_MenuManager.OnKeyPressed(KeyCode::Left);
    if (Input::IsKeyPressed(KeyCode::Right))    m_MenuManager.OnKeyPressed(KeyCode::Right);
    if (Input::IsKeyPressed(KeyCode::Escape))   m_MenuManager.OnKeyPressed(KeyCode::Escape);
    if (Input::IsKeyPressed(KeyCode::Enter))    m_MenuManager.OnKeyPressed(KeyCode::Enter);
  }

  // 注意：不在此处额外调用 WaitAll()。
  // DispatchSubsystemUpdates 已按阶段逐一等待完成，此处的显式同步是多余的，
  // 且在主线程 work-stealing 场景下可能导致不可预期的执行顺序。
}
void Application::InternalRender() {
  PROFILE_ZONE();
  auto context = m_Window->GetContext();
  context->ClearColor(0.2f, 0.2f, 0.2f, 1.0f);

  // 仅在需要时绘制默认测试四边形
  if (m_RenderDefaultQuad && m_Shader && m_VAO && m_Texture) {
    m_Shader->Bind();
    m_Shader->SetMat4("u_ViewProjection",
                      m_Camera.GetViewProjectionMatrixPtr());
    m_Texture->Bind(0);
    m_VAO->Bind();
    context->DrawIndexed(m_VAO);
  }

  // 子类扩展
  OnRender();
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

  // ── 初始化 Job 系统（线程池） ──
  // 传入 hardware_concurrency 个线程，预留 1 核给未来渲染线程
  JobSystem::Init(0, 1);

  OnStartup();

  // ── 初始化 Profiler（Tracy GPU 上下文 + 日志输出） ──
  Profiler::Init();
  PROFILE_FRAME_START;

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
      // 收集渲染统计（在构建 UI 前，让 DrawCall 计数反映上一帧的数据）
      uint32 drawCalls = m_Window->GetContext()->GetAndResetDrawCallCount();

      m_PerfWindow.FeedStats(dt * 1000.0f, drawCalls, 0, 0, 0, 0);

      // 绘制性能监控窗口（可由子类通过 m_DrawPerformanceWindow=false 交给
      // Editor 管理）
      if (m_DrawPerformanceWindow)
        m_PerfWindow.OnImGui();

      // 子类自定义 UI
      OnImGui();

      // ── 绘制内置菜单 ──
      if (m_MenuManager.isVisible)
        m_MenuManager.OnImGui();
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

  // ── 先关闭 UI（必须在 SubsystemManager 析构前，因为后者会销毁窗口，
  //     而 ImGui 后端清理需要有效的 GLFW/OpenGL 上下文） ──
  UIManager::Shutdown();

  // ── Job 系统关闭 ──
  JobSystem::Shutdown();
}

} // namespace Engine