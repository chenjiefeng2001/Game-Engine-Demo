#pragma once

/**
 * @file SubsystemConfig.h
 * @brief 子系统更新策略配置 — 每个子系统可独立声明自己的驱动方式
 *
 * 设计目的：
 *   不同的子系统有不同的更新需求。物理系统需要固定步长，粒子系统可以
 *   限频更新，音频系统可以事件驱动，UI 可以每帧更新。此配置允许每个
 *   子系统声明自己的策略，由 Application 的混合驱动调度器统一管理。
 *
 * 使用方式：
 * @code
 *   // 注册一个使用固定步长更新的子系统
 *   RegisterUpdateSubsystem("Physics",
 *       [this](float32 dt) { m_PhysicsWorld->Step(dt); },
 *       SubsystemConfig::Fixed(1.0f / 60.0f)
 *   );
 *
 *   // 注册一个限频 30Hz 的子系统
 *   RegisterUpdateSubsystem("Particles",
 *       [this](float32 dt) { m_ParticleSystem->Update(dt); },
 *       SubsystemConfig::Throttled(30.0f)
 *   );
 *
 *   // 注册一个后台也继续运行的子系统（音频）
 *   RegisterUpdateSubsystem("Audio",
 *       [this](float32 dt) { m_AudioSystem->Update(dt); },
 *       SubsystemConfig::Default().WithBackground(true)
 *   );
 * @endcode
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <functional>

namespace Engine {

/**
 * @brief 子系统执行阶段 — 决定多线程并行时的执行顺序
 *
 * 同一阶段的子系统可以并行执行（共享 Job 线程池）。
 * 不同阶段的子系统按顺序串行执行，保证依赖安全。
 *
 * 示例时序：
 *   PrePhysics  [Input] [AI Decision]        ← 这两者并行
 *   Physics     [Physics Step]                ← Box2D 单线程
 *   PostPhysics [Particles] [Animation Blend] ← 这两者并行
 *   LateUpdate  [Audio] [Menu] [UI]           ← 这两者并行
 */
enum class SubsystemExecPhase : uint8 {
    PrePhysics  = 0,
    Physics     = 1,
    PostPhysics = 2,
    LateUpdate  = 3,
    COUNT
};

/**
 * @brief 子系统更新方式枚举
 */
enum class SubsystemUpdateMode : uint8 {
    Default,        ///< 跟随 LoopMode 的默认策略
    Variable,       ///< 可变步长：每帧都跑，dt 为真实时间
    Fixed,          ///< 固定步长：按 fixedDt 累加，可能一帧多次或零次
    Throttled,      ///< 限频：最大 maxHz 帧每秒，其余帧跳过
    EventDriven,    ///< 事件驱动：仅在外部标记需要更新时才跑
    Manual          ///< 手动控制：Application 不自动调用，子系统自行调度
};

/**
 * @brief 子系统更新配置
 *
 * 每个子系统注册时附带一个 SubsystemConfig，声明其更新偏好。
 * Application::InternalUpdate() 根据这些配置选择不同的调度策略。
 *
 * 提供了方便的静态工厂方法：
 *   SubsystemConfig::Default()
 *   SubsystemConfig::Fixed(1.0f/60.0f)
 *   SubsystemConfig::Throttled(30.0f)
 *   SubsystemConfig::EventDriven()
 *   SubsystemConfig::Manual()
 */
struct SubsystemConfig {
    SubsystemUpdateMode updateMode      = SubsystemUpdateMode::Default;
    SubsystemExecPhase  execPhase       = SubsystemExecPhase::PostPhysics;  // 默认物理后
    float32             maxHz           = 60.0f;
    float32             fixedDt         = 1.0f / 60.0f;
    bool                runInBackground = true;
    bool                canSkipRender   = true;

    // ── 静态工厂方法 ──

    static SubsystemConfig Default()    { return {}; }

    static SubsystemConfig Fixed(float32 dt, SubsystemExecPhase phase = SubsystemExecPhase::Physics) {
        SubsystemConfig cfg;
        cfg.updateMode = SubsystemUpdateMode::Fixed;
        cfg.fixedDt    = dt;
        cfg.execPhase  = phase;
        return cfg;
    }

    static SubsystemConfig Throttled(float32 hz, SubsystemExecPhase phase = SubsystemExecPhase::PostPhysics) {
        SubsystemConfig cfg;
        cfg.updateMode = SubsystemUpdateMode::Throttled;
        cfg.maxHz      = hz;
        cfg.execPhase  = phase;
        return cfg;
    }

    static SubsystemConfig EventDriven(SubsystemExecPhase phase = SubsystemExecPhase::LateUpdate) {
        SubsystemConfig cfg;
        cfg.updateMode = SubsystemUpdateMode::EventDriven;
        cfg.execPhase  = phase;
        return cfg;
    }

    static SubsystemConfig Manual() {
        SubsystemConfig cfg;
        cfg.updateMode = SubsystemUpdateMode::Manual;
        return cfg;
    }

    // ── Fluent 修改器 ──

    SubsystemConfig& WithPhase(SubsystemExecPhase phase) {
        execPhase = phase;
        return *this;
    }

    SubsystemConfig& WithBackground(bool run) {
        runInBackground = run;
        return *this;
    }

    SubsystemConfig& WithCanSkipRender(bool skip) {
        canSkipRender = skip;
        return *this;
    }
};

/**
 * @brief 更新子系统条目（Application 内部使用）
 *
 * 记录子系统注册时的信息 + 运行时调度状态。
 */
struct SubsystemUpdateEntry {
    uint32      id = 0;
    std::string name;
    std::function<void(float32 dt)> updateFn;
    SubsystemConfig config;

    // ── 运行时调度状态（子系统内部使用，Application 管理） ──
    float32 accumulator     = 0.0f;   // Fixed 模式累加器
    float32 timeSinceLast   = 0.0f;   // Throttled 上次更新经过时间
    bool    eventPending    = false;  // EventDriven 标记
    uint64  currentJobId    = 0;      // 当前帧的 Job ID（用于等待完成）

    /** 标记 EventDriven 子系统需要更新（由外部事件触发时调用） */
    void MarkDirty() { eventPending = true; }
};

} // namespace Engine
