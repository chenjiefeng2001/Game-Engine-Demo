#pragma once

/**
 * @file SubsystemManager.h
 * @brief 子系统管理器 — 按阶段管理引擎各子系统的初始化/关闭顺序
 *
 * 设计思想：
 *   - 将启动过程划分为多个阶段（SubsystemPhase），阶段间严格顺序执行
 *   - 同一阶段内的多个子系统按注册顺序执行
 *   - 关闭时逆序执行（后初始化的先关闭），保证依赖安全
 *   - 纯数据驱动，不依赖任何第三方库
 */

#include "Engine/Types.h"
#include <functional>
#include <string>
#include <vector>

namespace Engine {

// ============================================================
// 子系统阶段枚举（按执行顺序排列，值越小越先执行）
// ============================================================
enum class SubsystemPhase : uint8 {
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
// 子系统描述 — 描述一个可初始化的子系统单元
// ============================================================
struct SubsystemDesc {
    std::string              name;        ///< 子系统名称（日志/调试用）
    SubsystemPhase           phase;       ///< 所属阶段
    std::function<bool()>    onInit;      ///< 初始化回调，返回 true 表示成功
    std::function<void()>    onShutdown;  ///< 可选的关闭回调（逆序执行）

    SubsystemDesc() = default;

    SubsystemDesc(std::string name_, SubsystemPhase phase_,
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
// 子系统管理器 — 管理引擎各子系统的初始化/关闭顺序
// ============================================================
class SubsystemManager {
public:
    SubsystemManager() = default;
    ~SubsystemManager() { Shutdown(); }

    // 禁止拷贝
    SubsystemManager(const SubsystemManager&) = delete;
    SubsystemManager& operator=(const SubsystemManager&) = delete;

    /**
     * @brief 注册一个子系统
     * @param desc 子系统描述（名称、阶段、初始化回调、关闭回调）
     */
    SubsystemManager& Add(const SubsystemDesc& desc);

    /**
     * @brief 注册一个子系统（便捷重载）
     * @param name     子系统名称
     * @param phase    所属阶段
     * @param init     初始化回调
     * @param shutdown 可选关闭回调
     */
    SubsystemManager& Add(std::string name, SubsystemPhase phase,
                          std::function<bool()> init,
                          std::function<void()> shutdown = nullptr);

    // ── 生命周期 ──

    /**
     * @brief 按阶段顺序初始化所有子系统
     * @return true 全部成功，false 有子系统失败
     *
     * 一旦某子系统失败，后续子系统不再初始化。
     * 已成功的子系统可在 Shutdown() 时逆序清理。
     */
    bool Initialize();

    /**
     * @brief 逆序关闭所有已初始化的子系统
     *
     * 保证后初始化的先关闭，满足依赖逆序。
     */
    void Shutdown();

    // ── 查询 ──

    /** 检查是否已注册指定名称的子系统 */
    bool HasSubsystem(const std::string& name) const;

    /** 已注册的子系统总数 */
    size_t SubsystemCount() const { return m_Subsystems.size(); }

    /** 是否已完成初始化 */
    bool IsInitialized() const { return m_Initialized; }

    /** 获取初始化过程中的错误列表 */
    const std::vector<std::string>& GetErrors() const { return m_Errors; }

    /** 获取所有已注册子系统的名称列表 */
    std::vector<std::string> GetSubsystemNames() const;

private:
    struct SubsystemEntry {
        SubsystemDesc           desc;
    };

    std::vector<SubsystemEntry>  m_Subsystems;
    std::vector<std::string>     m_Errors;
    std::vector<size_t>          m_Succeeded;   // 成功初始化的子系统在 m_Subsystems 中的索引

    bool m_Initialized = false;
};

} // namespace Engine
