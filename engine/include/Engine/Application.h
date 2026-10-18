#pragma once

#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IWindow.h"
#include "Engine/Core/Memory/StackAllocator.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/Renderer/OrthographicCamera.h"
#include "Engine/Core/SubsystemManager.h"
#include "Engine/Core/SubsystemConfig.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/MenuManager.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/PerformanceWindow.h"
#include "Engine/Types.h"
#include "Engine/UIManager.h"
#include "Engine/Editor/EditorCamera.h"
#include "Engine/Editor/ViewportPanel.h"
#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include <memory>
#include <vector>
#include <unordered_map>

// 前向声明
struct GladGLContext;
class OpenGLFramebuffer;


namespace Engine {

class IWindow;
class Shader;
class VertexArray;
class Texture;
class TextureManager;
class IGraphicsFactory;
class OrthographicCamera;
class SceneRenderer;
class Scene;
class ConsolePanel;

enum class LoopMode {
  Variable, // 可变步长：每帧 dt 取决于实际时间
  Fixed,    // 固定步长：物理/逻辑固定 60Hz，渲染按实际帧率
  Adaptive  // 自适应：激活时正常渲染，后台时降帧/阻塞等待
};

/**
 * @brief 引擎应用基类
 *
 * 使用 SubsystemManager 管理所有子系统的初始化/关闭顺序：
 *   1. 子类在构造函数中通过 RegisterSubsystem() 注册各子系统
 *   2. Run() 调用 SubsystemManager::Initialize() 按阶段顺序启动
 *   3. 析构时自动逆序关闭
 *
 * 示例：
 *   class MyApp : public Application {
 *       MyApp(IGraphicsFactory& factory) : Application(factory) {
 *           RegisterSubsystem("MySystem", SubsystemPhase::Custom, [this]{ ...
 * });
 *       }
 *   };
 */
class Application {
public:
  // ── 构造 / 析构 ──
  Application(IGraphicsFactory &factory, bool headless = false);
  virtual ~Application();

  // ── 全局实例（Phase 7 bridge 访问用） ──
  static Application* s_Instance;
  static Application* GetInstance() { return s_Instance; }
  static bool IsSubsystemAllocatorPresent();

  // ── 主循环 ──
  void Run();

  bool InitializeHeadless();
  bool PumpOneFrame(float32 dt);
  void Shutdown();
  bool IsInitialized() const { return m_Initialized; }
  bool IsHeadless() const { return m_Headless; }

  // ── HRC-3 P1：production-owned scene render（headless 也能真的画场景）──
  //
  // 语义：把 session 场景里的**真实 renderable**（SpriteComponent）经由 production
  // SceneRenderer / ISpriteBatch 画进**当前已绑定**的 framebuffer。
  //
  // 约束（见 docs/Editor-Avalonia-HRC3-P1P2-Contract.md）：
  // - P1-H1/H2：与 ViewportPanel / EngineEditor **完全无关**（不调用 ViewportPanel::OnUpdate，
  //   不依赖 legacy ViewportPanel 的 MRT FBO / ImGui / readback 缓冲）；
  // - P1-H3：渲染由既有 frame/pump 驱动，本类不引入任何 tick 源；
  // - P1-H5：只画真实场景内容 —— **没有 clear-only 成功**，场景无可渲染内容时返回 false；
  // - P1-H7：失败一律返回 false，由调用方转成 typed failure，不得静默降级为 clear-only。
  //
  // 首次调用时惰性创建 sprite-batch 渲染资源（复用 assets/shaders/sprite_batch.{vert,frag}，
  // 与 OpenGLSpriteBatch 的顶点布局一致）；创建失败返回 false。
  bool InitProductionRenderResources(IRenderContext &context);
  bool RenderProductionFrame(IRenderContext &context, Scene &scene,
                             uint32_t targetWidth, uint32_t targetHeight);
  /// 上一次 RenderProductionFrame 绘制的 renderable 数量（0 = 没有真实内容）。
  uint32_t LastProductionRenderableCount() const { return m_ProductionRenderableCount; }

  /// HRC-3 D3：production pick pass 开关（默认**关闭**）
  ///
  /// D3 Step 2 冻结的 U2 语义是「pick-ID pass **仅当存在 pending request** 时运行」。
  /// 目前还没有任何请求通道（属于后续的 D3 ABI mini-contract），因此默认必须关闭：
  /// 每帧无条件跑 ID pass 既是纯浪费，实测还会在当前 production teardown 路径上挂起。
  /// 真正的按需触发由后续 ABI 负责；在此之前只有 native 证明夹具显式打开它。
  void SetProductionPickPassEnabled(bool enabled) noexcept {
    m_ProductionPickPassEnabled = enabled;
  }
  bool IsProductionPickPassEnabled() const noexcept {
    return m_ProductionPickPassEnabled;
  }






  // ── 访问器 ──
  IGraphicsFactory &GetFactory() { return m_Factory; }
  TextureManager &GetTextureManager() { return m_TextureManager; }
  IWindow &GetWindow() { return *m_Window; }
  IRenderContext *GetRenderContext() {
    return m_Window ? m_Window->GetContext() : nullptr;
  }

  // ── Phase 7: Viewport FBO readback ──
  ViewportPanel& GetViewportPanel() { return m_ViewportPanel; }

  // ── 循环模式控制 ──
  LoopMode GetLoopMode() const { return m_LoopMode; }
  void SetLoopMode(LoopMode mode) { m_LoopMode = mode; }

  // ── 游戏播放状态控制（编辑器运行时切换） ──
  /** 是否处于游戏运行状态（编辑器内点击 Play/Stop 切换） */
  bool IsPlaying() const { return m_IsPlaying; }
  /**
   * @brief 设置游戏播放状态
   * @param playing true = 进入游戏运行模式（激活 MenuManager），false = 回到编辑模式
   */
  void SetPlaying(bool playing);

  /**
   * @brief 获取渲染插值因子（Fixed 模式下用于视觉平滑）
   * @return 0~1 的插值 alpha，非 Fixed 模式返回 1.0f
   */
  float32 GetRenderAlpha() const { return m_RenderAlpha; }

  // ============================================================
  // 混合驱动 API — 每个子系统可注册独立的更新策略
  // ============================================================

  /**
   * @brief 注册一个可独立配置更新策略的子系统
   *
   * @param name     子系统名称（调试/日志用）
   * @param updateFn 更新回调 void(float32 dt)
   * @param config   更新策略配置
   * @return 子系统 ID（用于取消注册 / MarkDirty）
   *
   * 示例：
   * @code
   *   // 物理：固定步长
   *   RegisterUpdateSubsystem("Physics",
   *       [this](float32 dt) { m_World.Step(dt); },
   *       SubsystemConfig::Fixed(1.0f/60.0f)
   *   );
   *
   *   // 粒子：限频 30Hz
   *   RegisterUpdateSubsystem("Particles",
   *       [this](float32 dt) { m_Particles.Update(dt); },
   *       SubsystemConfig::Throttled(30.0f)
   *   );
   *
   *   // 音频：后台继续运行
   *   RegisterUpdateSubsystem("Audio",
   *       [this](float32 dt) { m_Audio.Update(dt); },
   *       SubsystemConfig::Default().WithBackground(true)
   *   );
   * @endcode
   */
  uint32 RegisterUpdateSubsystem(const std::string& name,
                                  std::function<void(float32)> updateFn,
                                  const SubsystemConfig& config = {});

  /**
   * @brief 取消注册一个更新子系统
   * @param id RegisterUpdateSubsystem 返回的 ID
   */
  void UnregisterUpdateSubsystem(uint32 id);

  /**
   * @brief 标记一个 EventDriven 子系统需要更新
   * @param id 子系统 ID
   *
   * 在事件回调中调用，例如：
   *   MarkSubsystemDirty(m_AudioSubsystemId);
   */
  void MarkSubsystemDirty(uint32 id);

  // ── 控制台集成 ──
  /** 设置全局控制台面板指针（用于 ~ 键切换和输入阻塞） */
  static void SetConsolePanel(ConsolePanel* panel) { s_ConsolePanel = panel; }
  /** 获取全局控制台面板指针 */
  static ConsolePanel* GetConsolePanel() { return s_ConsolePanel; }

protected:
  /**
   * @brief 注册自定义子系统（由子类在构造时调用）
   *
   * @param name    子系统名称
   * @param phase   所属阶段
   * @param init    初始化回调
   * @param shutdown 可选关闭回调
   */
  void RegisterSubsystem(std::string name, SubsystemPhase phase,
                         std::function<bool()> init,
                         std::function<void()> shutdown = nullptr);

  // ── 可被子类重写的生命周期方法 ──
  /** 在所有系统初始化完成后调用 */
  virtual void OnStartup() {}
  /** 每帧更新 */
  virtual void OnUpdate(float32 dt) { (void)dt; }
  /** 每帧渲染 */
  virtual void OnRender() {}
  /** 每帧 UI 构建（在 Begin/End 之间调用，仅在 UI 可见时执行） */
  virtual void OnImGui() {}

  // ── 引擎成员（protected 供子类访问） ──
  // 注意：成员析构顺序 = 声明顺序的反序。
  // m_SubsystemAllocator 必须在 m_SubsystemManager 之后析构，
  // 因为 SubsystemManager::Shutdown() 中注册的 lambda 可能引用
  // StackAllocator 分配的内存，如果在 SubsystemManager 析构前
  // 释放内存池会导致 use-after-free。
  // 所以 m_SubsystemAllocator 声明在 m_SubsystemManager 之前。
  StackAllocator m_SubsystemAllocator{256 * 1024}; // 256KB 连续内存池
  IGraphicsFactory &m_Factory;
    std::unique_ptr<class IWindow> m_Window;
    TextureManager m_TextureManager;
  std::shared_ptr<class Shader> m_Shader;
  std::shared_ptr<class VertexArray> m_VAO;
  std::shared_ptr<class Texture> m_Texture;
  OrthographicCamera m_Camera; // 直接成员，零动态分配
  PerformanceWindow m_PerfWindow;
  MenuManager m_MenuManager;

      /** 视口面板（编辑器嵌入） */
  ViewportPanel m_ViewportPanel{"Viewport"};

  /** 编辑器相机 */
  EditorCamera m_EditorCamera;

  /** 是否处于游戏运行状态（编辑器 Play/Stop 控制） */
  bool m_IsPlaying = false;

  /** 是否由 Application::Run() 自动绘制性能窗口。设为 false 可交给 Editor 管理
   */
  bool m_DrawPerformanceWindow = true;
  bool m_RenderDefaultQuad = true;

  /**
   * @brief 是否使用 EngineEditor 自管理的 DockSpace 布局。
   * 设为 true 时，Application::Run() 中的 DockspaceBuilder 会被跳过，
   * 由 EngineEditor::OnImGui() 自行创建全屏 DockSpace 窗口（含内嵌菜单栏）。
   * 适用于集成了 EngineEditor 的编辑器应用。
   */
  bool m_UseEngineEditorDockspace = false;

  // ── 混合驱动调度内部锁 ──
  bool m_InsideDispatch = false;

private:
  // ── 引擎内置初始化步骤 ──
  bool InitWindow();
  bool InitCamera();
  bool InitUI();
  bool InitShader();
  bool InitVertexData();

  // ── 视口 FBO ──
  void InitViewportFBO();           // 第一次初始化
  void ResizeViewportFBO(int w, int h);  // resize 回调
  std::unique_ptr<OpenGLFramebuffer> m_ViewportFBO;
  SubsystemManager m_SubsystemManager;
  GladGLContext* m_GL = nullptr;   // 用于创建 FBO

  // ── 内部循环 ──
  void InternalUpdate(float32 dt);
  void InternalRender();

  // ── HRC-3 P1：production render 资源（惰性创建，见公开接口注释）──
  std::shared_ptr<class Shader> m_ProductionSpriteShader;
  std::shared_ptr<class SceneRenderer> m_ProductionSceneRenderer;
  std::unique_ptr<OrthographicCamera> m_ProductionCamera;
  bool m_ProductionRenderReady = false;
  uint32_t m_ProductionRenderableCount = 0;

  // ── HRC-3 D3：production pick pass 资源 ──
  // 遵循 D3 Step 2 冻结的 U1/U3：R32I attachment 1 + 专用 pick shader。
  // 与 color pass **共用同一批 RenderCommand / SpriteData / camera**，
  // 几何由同一个 sprite batch 路径光栅化 —— 两个 pass 若发生偏移，
  // ID 会静默落到错误像素上，D3-C1 明确禁止这种"看起来合理"的结果。
  std::shared_ptr<class Shader> m_ProductionPickShader;
  std::shared_ptr<class SceneRenderer> m_ProductionPickSceneRenderer;
  bool m_ProductionPickReady = false;
  uint32_t m_ProductionPickEntityCount = 0;
  bool m_ProductionPickPassEnabled = false;

  LoopMode m_LoopMode = LoopMode::Variable;
  bool m_Headless = false;
  bool m_Initialized = false;
  bool m_ShutdownComplete = false;

  // ── 混合驱动调度（按名称索引 + 按 ID 索引） ──
  std::unordered_map<std::string, uint32> m_SubsystemNameMap;
  std::unordered_map<uint32, SubsystemUpdateEntry> m_SubsystemEntries;
  uint32 m_NextSubsystemId = 1000;  // 从 1000 开始，避免与零值冲突

  // ── 渲染插值因子（Fixed 模式使用） ──
  float32 m_RenderAlpha = 1.0f;

  /** InternalUpdate 内部：按配置分发更新到各子系统 */
  void DispatchSubsystemUpdates(float32 dt);

  // ── 控制台 ──
  static ConsolePanel* s_ConsolePanel;
};

} // namespace Engine