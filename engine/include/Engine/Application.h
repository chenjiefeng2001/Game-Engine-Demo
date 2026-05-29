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
#include "Engine/PerformanceWindow.h"
#include "Engine/Types.h"
#include "Engine/UIManager.h"
#include <memory>
#include <vector>
#include <unordered_map>


namespace Engine {

class IWindow;
class Shader;
class VertexArray;
class Texture;
class TextureManager;
class IGraphicsFactory;
class OrthographicCamera;

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
  Application(IGraphicsFactory &factory);
  virtual ~Application();

  // ── 主循环 ──
  void Run();

  // ── 访问器 ──
  IGraphicsFactory &GetFactory() { return m_Factory; }
  TextureManager &GetTextureManager() { return m_TextureManager; }
  IWindow &GetWindow() { return *m_Window; }
  IRenderContext *GetRenderContext() {
    return m_Window ? m_Window->GetContext() : nullptr;
  }

  // ── 循环模式控制 ──
  LoopMode GetLoopMode() const { return m_LoopMode; }
  void SetLoopMode(LoopMode mode) { m_LoopMode = mode; }

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
  SubsystemManager m_SubsystemManager;
  StackAllocator m_SubsystemAllocator{256 * 1024}; // 256KB 连续内存池
  IGraphicsFactory &m_Factory;
  TextureManager m_TextureManager;
  std::unique_ptr<class IWindow> m_Window;
  std::shared_ptr<class Shader> m_Shader;
  std::shared_ptr<class VertexArray> m_VAO;
  std::shared_ptr<class Texture> m_Texture;
  OrthographicCamera m_Camera; // 直接成员，零动态分配
  PerformanceWindow m_PerfWindow;
  MenuManager m_MenuManager;

  /** 是否由 Application::Run() 自动绘制性能窗口。设为 false 可交给 Editor 管理
   */
  bool m_DrawPerformanceWindow = true;
  bool m_RenderDefaultQuad = true;

private:
  // ── 引擎内置初始化步骤 ──
  bool InitWindow();
  bool InitCamera();
  bool InitUI();
  bool InitShader();
  bool InitVertexData();

  // ── 内部循环 ──
  void InternalUpdate(float32 dt);
  void InternalRender();


  LoopMode m_LoopMode = LoopMode::Variable;

  // ── 混合驱动调度（按名称索引 + 按 ID 索引） ──
  std::unordered_map<std::string, uint32> m_SubsystemNameMap;
  std::unordered_map<uint32, SubsystemUpdateEntry> m_SubsystemEntries;
  uint32 m_NextSubsystemId = 1000;  // 从 1000 开始，避免与零值冲突

  // ── 渲染插值因子（Fixed 模式使用） ──
  float32 m_RenderAlpha = 1.0f;

  /** InternalUpdate 内部：按配置分发更新到各子系统 */
  void DispatchSubsystemUpdates(float32 dt);
};

} // namespace Engine