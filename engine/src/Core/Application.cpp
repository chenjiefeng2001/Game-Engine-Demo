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
#include "Engine/UIManager.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace Engine {

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
    std::cerr << "[Application] Failed to create window" << std::endl;
    return false;
  }
  std::cout << "[Application] Window created (800x600)" << std::endl;
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
    std::cerr << "[Application] Failed to create shader" << std::endl;
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
    std::cerr << "[Application] Failed to create vertex buffer" << std::endl;
    return false;
  }

  auto ib =
      m_Factory.CreateIndexBuffer(indices, sizeof(indices) / sizeof(uint32));
  if (!ib) {
    std::cerr << "[Application] Failed to create index buffer" << std::endl;
    return false;
  }

  m_VAO = m_Factory.CreateVertexArray();
  m_VAO->AddVertexBuffer(vb);
  m_VAO->SetIndexBuffer(ib);

  std::cout << "[Application] Vertex data initialized"
            << " (4 vertices, 6 indices)" << std::endl;
  return true;
}

bool Application::InitUI() {
  auto ui = m_Factory.CreateUIManager();
  if (!ui) {
    std::cerr << "[Application] Failed to create UI manager" << std::endl;
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
    std::cerr << "[Application] Subsystem '" << name
              << "' already registered (ID=" << it->second
              << "), returning existing ID." << std::endl;
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

  std::cout << "[Application] RegisterUpdateSubsystem: '" << name
            << "' (ID=" << id << ", mode=" << static_cast<int>(config.updateMode)
            << ")" << std::endl;
  return id;
}

void Application::UnregisterUpdateSubsystem(uint32 id) {
  auto it = m_SubsystemEntries.find(id);
  if (it == m_SubsystemEntries.end()) {
    std::cerr << "[Application] UnregisterUpdateSubsystem: ID=" << id
              << " not found." << std::endl;
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

  std::cout << "[Application] UnregisterUpdateSubsystem: '"
            << it->second.name << "' (ID=" << id << ")" << std::endl;
  m_SubsystemEntries.erase(it);
}

void Application::MarkSubsystemDirty(uint32 id) {
  auto it = m_SubsystemEntries.find(id);
  if (it != m_SubsystemEntries.end()) {
    it->second.eventPending = true;
  }
}

// ============================================================
// 混合驱动调度器 — 按各子系统配置分发更新
// ============================================================

void Application::DispatchSubsystemUpdates(float32 dt) {
  bool isActive = true;
  if (m_LoopMode == LoopMode::Adaptive) {
    isActive = m_Window->IsActive() && !m_Window->IsMinimized();
  }

  for (auto& [id, entry] : m_SubsystemEntries) {
    const auto& config = entry.config;

    // ── 后台模式过滤 ──
    if (!isActive && !config.runInBackground) {
      continue;  // 该子系统在后台不运行
    }

    // ── 根据 UpdateMode 选择调度策略 ──
    switch (config.updateMode) {

      case SubsystemUpdateMode::Default: {
        // Default = 跟随 LoopMode 的默认策略
        // Variable / Adaptive → 每帧更新
        // 已有代码通过 OnUpdate() 处理
        if (entry.updateFn) entry.updateFn(dt);
        break;
      }

      case SubsystemUpdateMode::Variable: {
        // 可变步长：每帧都跑
        if (entry.updateFn) entry.updateFn(dt);
        break;
      }

      case SubsystemUpdateMode::Fixed: {
        // 固定步长累加器
        entry.accumulator += dt;
        float32 fixedDt = config.fixedDt;
        while (entry.accumulator >= fixedDt) {
          if (entry.updateFn) entry.updateFn(fixedDt);
          entry.accumulator -= fixedDt;
        }
        break;
      }

      case SubsystemUpdateMode::Throttled: {
        // 限频更新
        entry.timeSinceLast += dt;
        float32 minInterval = 1.0f / config.maxHz;
        if (entry.timeSinceLast >= minInterval) {
          if (entry.updateFn) entry.updateFn(entry.timeSinceLast);
          entry.timeSinceLast = 0.0f;
        }
        break;
      }

      case SubsystemUpdateMode::EventDriven: {
        // 事件驱动：仅在 markDirty 被调用时更新
        if (entry.eventPending) {
          if (entry.updateFn) entry.updateFn(dt);
          entry.eventPending = false;
        }
        break;
      }

      case SubsystemUpdateMode::Manual: {
        // 手动控制：Application 不自动调用
        break;
      }
    }
  }
}

void Application::InternalUpdate(float32 dt) {
  // ── 阶段 A：引擎级输入处理 ──
  if (Input::IsKeyDown(KeyCode::W)) {
    // m_Camera->MoveForward(dt);
  }
  if (Input::IsKeyDown(KeyCode::A)) {
    // m_Camera->MoveLeft(dt);
  }

  float32 dx = Input::GetMouseDeltaX();
  float32 dy = Input::GetMouseDeltaY();
  // m_Camera->Rotate(dx, dy);

  // ── 阶段 B：子类扩展（传统的每帧 OnUpdate 仍有效） ──
  OnUpdate(dt);

  // ── 阶段 C：混合驱动调度 ──
  // 所有通过 RegisterUpdateSubsystem 注册的子系统按各自配置更新
  DispatchSubsystemUpdates(dt);

  // ── 阶段 D：更新菜单状态 ──
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
}
void Application::InternalRender() {
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

  // ── 将栈分配器注入工厂，所有子系统对象从连续内存池分配 ──
  m_Factory.SetAllocator(&m_SubsystemAllocator);

  if (!m_SubsystemManager.Initialize()) {
    std::cerr << "[Application] Subsystem initialization failed, aborting."
              << std::endl;
    return;
  }

  OnStartup();

  // ── 启动文件监视器（在资源加载完成后） ──
  if (auto *fw = FileWatcher::Get())
    fw->Start();

  // ── 初始化高精度计时器 ──
  Time::Init();

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

        // 失去焦点但未最小化：限制 10fps 让后台任务继续
        Time::UpdateDeltaTime();
        float32 dt = Time::GetDeltaTime();
        if (dt < 0.1f) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
    if (auto *ui = UIManager::Get())
      ui->Begin();

    // ============================================================
    // 阶段 4：更新（混合驱动 — 全局策略 + 子系统独立策略）
    // ============================================================
    if (m_LoopMode == LoopMode::Fixed) {
      // 固定步长模式 (Fixed 60Hz) 带漂移修正
      static const float32 fixedDt = 1.0f / 60.0f;
      static float64 acc = 0.0;

      // 每 600 帧（~10 秒）用绝对时间校准一次累加器，消除累积漂移
      static uint32 calibrateCounter = 0;
      if (++calibrateCounter >= 600) {
        acc = Time::CalibrateAccumulator(acc, fixedDt);
        calibrateCounter = 0;
      }

      acc += dt;
      while (acc >= fixedDt) {
        InternalUpdate(fixedDt);
        acc -= fixedDt;
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
    // 阶段 8：Swap Buffers + 帧尾消息泵
    // ============================================================
    m_Window->OnUpdate();
  }
}

} // namespace Engine