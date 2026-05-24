#pragma once

/**
 * @file StartupQueue.h
 * @brief 启动队列 — 按阶段管理引擎各系统的初始化/关闭顺序
 *
 * 设计思想：
 *   - 将启动过程划分为多个阶段（StartupPhase），阶段间严格顺序执行
 *   - 同一阶段内的多个步骤按添加顺序执行
 *   - 关闭时逆序执行（后初始化的先关闭），保证依赖安全
 *   - 纯数据驱动，不依赖任何第三方库
 */

#include "Engine/Types.h"
#include <functional>
#include <string>
#include <vector>

namespace Engine {

// ============================================================
// 启动阶段枚举（按执行顺序排列，值越小越先执行）
// ============================================================
enum class StartupPhase : uint8 {
    Core        = 0,    // 核心系统（日志、内存分配器等）
    Platform    = 1,    // 平台层（窗口、上下文创建）
    Graphics    = 2,    // 图形 API 初始化
    Input       = 3,    // 输入系统
    Resources   = 4,    // 资源管理器
    Assets      = 5,    // 资源加载（纹理、模型等）
    Scene       = 6,    // 场景/游戏对象
    Game        = 7,    // 游戏逻辑
    Custom      = 8,    // 用户自定义
    COUNT
};

// ============================================================
// 启动步骤 — 描述一个可初始化的系统单元
// ============================================================
struct StartupStep {
    std::string              name;        ///< 步骤名称（日志/调试用）
    StartupPhase             phase;       ///< 所属阶段
    std::function<bool()>    onInit;      ///< 初始化回调，返回 true 表示成功
    std::function<void()>    onShutdown;  ///< 可选的关闭回调（逆序执行）

    StartupStep() = default;

    StartupStep(std::string name_, StartupPhase phase_,
                std::function<bool()> init_,
                std::function<void()> shutdown_ = nullptr)
        : name(std::move(name_))
        , phase(phase_)
        , onInit(std::move(init_))
        , onShutdown(std::move(shutdown_))
    {
    }
};

// ============================================================
// 启动队列 — 管理引擎各系统的初始化/关闭顺序
// ============================================================
class StartupQueue {
public:
    StartupQueue() = default;
    ~StartupQueue() { Shutdown(); }

    // 禁止拷贝
    StartupQueue(const StartupQueue&) = delete;
    StartupQueue& operator=(const StartupQueue&) = delete;

    StartupQueue& Add(const StartupStep& step);

    StartupQueue& Add(std::string name, StartupPhase phase,
                      std::function<bool()> init,
                      std::function<void()> shutdown = nullptr);

    // ── 执行 ──

    /**
     * @brief 按阶段顺序执行所有步骤
     * @return true 全部成功，false 有步骤失败
     *
     * 一旦某步骤失败，后续步骤不再执行。
     * 已成功的步骤可在 Shutdown() 时逆序清理。
     */
    bool Execute();

    /**
     * @brief 逆序执行所有已成功步骤的 onShutdown
     *
     * 保证后初始化的先关闭，满足依赖逆序。
     */
    void Shutdown();

    // ── 查询 ──

    bool HasStep(const std::string& name) const;


    size_t StepCount() const { return m_Steps.size(); }

    bool IsInitialized() const { return m_Initialized; }
    const std::vector<std::string>& GetErrors() const { return m_Errors; }

    std::vector<std::string> GetStepNames() const;

private:
    std::vector<StartupStep> m_Steps;
    std::vector<std::string> m_Errors;
    std::vector<size_t> m_Succeeded;

    bool m_Initialized = false;
};

} // namespace Engine
