#pragma once

/**
 * @file ThreadAffinity.h
 * @brief 线程仿射平台抽象 — 核心绑定、线程命名、硬件拓扑查询
 *
 * 设计目标：
 *   1. 将工作线程固定到特定 CPU 核心，提升缓存命中率
 *   2. 区分 P-Core（性能核）和 E-Core（能效核），智能分配工作负载
 *   3. 为调试器（Visual Studio、RenderDoc）提供可读的线程名称
 *   4. 提供硬件拓扑查询接口（核心数、缓存大小、混合架构检测）
 *
 * 平台支持：
 *   - Windows: SetThreadAffinityMask / SetThreadDescription / GetLogicalProcessorInformation
 *   - Linux:   pthread_setaffinity_np / pthread_setname_np / sysconf
 *   - macOS:   thread_policy_set (有限支持)
 */

#include "Engine/Types.h"
#include <thread>

namespace Engine {
namespace Threading {

    // ============================================================
    // 拓扑信息
    // ============================================================
    struct TopologyInfo {
        /**
         * @brief 获取逻辑核心总数（含超线程）
         *
         * Windows: GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)
         * Linux:   sysconf(_SC_NPROCESSORS_ONLN)
         */
        static uint32 GetCoreCount();

        /**
         * @brief 获取性能核心数（P-Core）
         *
         * Intel ADL/RaptorLake/MeteorLake: P-Core 数量
         * AMD: CCD 全核视为 P-Core
         * 不支持时返回总核心数
         */
        static uint32 GetPECoreCount();

        /**
         * @brief 获取能效核心数（E-Core）
         *
         * 不支持时返回 0
         */
        static uint32 GetECoreCount();

        /**
         * @brief 检测是否为混合架构（Intel ADL+ 或 ARM big.LITTLE）
         */
        static bool IsHybridArchitecture();

        /**
         * @brief 获取最后一级缓存大小（字节）
         *
         * 用于调整 Job 粒度（批次大小应适配 LLC 缓存线）
         */
        static uint32 GetLLCCacheSize();
    };

    // ============================================================
    // 线程类别
    // ============================================================
    /**
     * @brief 线程类别 — 用于三池调度和核心分配策略
     *
     * 核心分配策略（混合架构下）：
     *   Main    → P-Core 0（固定，处理 OS 消息和 ImGui）
     *   Render  → P-Core 1（固定，避免与 OS 抢占）
     *   Worker  → P-Core 2..N-1（游戏逻辑、物理、AI）
     *   IO      → E-Core（低功耗核，适合异步流加载）
     */
    enum class ThreadCategory : uint8 {
        Main    = 0,   ///< 主线程：OS 消息泵、帧逻辑、ImGui
        Render  = 1,   ///< 渲染线程：Command Buffer 录制
        Worker  = 2,   ///< 工作线程池：物理/动画/剔除
        IO      = 3,   ///< IO 线程：文件流/网络异步加载
        COUNT
    };

    inline const char* ThreadCategoryName(ThreadCategory cat) noexcept {
        switch (cat) {
            case ThreadCategory::Main:   return "Main";
            case ThreadCategory::Render: return "Render";
            case ThreadCategory::Worker: return "Worker";
            case ThreadCategory::IO:     return "IO";
            default: return "Unknown";
        }
    }

    // ============================================================
    // 线程仿射 API
    // ============================================================

    /**
     * @brief 将线程绑定到指定的逻辑核心
     *
     * @param handle      线程原生句柄
     * @param coreIndex   目标逻辑核心编号（0-based）
     * @return true 表示绑定成功
     *
     * 平台实现：
     *   Windows: SetThreadAffinityMask(handle, 1ULL << coreIndex)
     *   Linux:   pthread_setaffinity_np(pthread_t, mask)
     */
    bool SetThreadAffinity(std::thread::native_handle_type handle,
                           uint32 coreIndex);

    /**
     * @brief 将线程绑定到核心掩码（允许多核心）
     *
     * @param handle   线程原生句柄
     * @param coreMask 核心位掩码（bit 0 = core 0, bit 1 = core 1...）
     * @return true 表示绑定成功
     */
    bool SetThreadAffinityMask(std::thread::native_handle_type handle,
                               uint64 coreMask);

    /**
     * @brief 设置当前线程的理想处理器（仅 Windows）
     *
     * @param coreIndex 目标核心编号
     *
     * 与强制绑定的区别：Ideal Processor 仅在 OS 调度时提供偏好，
     * 不会强制禁止线程迁移。适合不要求绝对隔离的场景。
     */
    void SetThreadIdealProcessor(uint32 coreIndex);

    /**
     * @brief 设置可读的线程名称（调试器可见）
     *
     * @param name 线程名称（UTF-8，建议 ≤ 64 字符）
     *
     * Windows: SetThreadDescription (Visual Studio / WinDbg 可见)
     * Linux:   pthread_setname_np (gdb / htop 可见)
     */
    void SetThreadName(const char* name);

    /**
     * @brief 获取当前线程的类别（用于日志和调试）
     *
     * 通过 TLS 存储，在线程启动时由调用方设置。
     */
    ThreadCategory GetCurrentThreadCategory() noexcept;

    /**
     * @brief 设置当前线程的类别（应在线程入口函数开头调用）
     */
    void SetCurrentThreadCategory(ThreadCategory cat) noexcept;

    // ============================================================
    // 核心分配策略辅助
    // ============================================================

    /**
     * @brief 根据线程类别计算推荐的逻辑核心索引
     *
     * @param cat           线程类别
     * @param workerIndex   对于 Worker 类别：池内索引（0-based）
     * @return 推荐的逻辑核心编号，0-based
     *
     * 混合架构下：
     *   Main   → core 0
     *   Render → core 1
     *   Worker → core 2 + workerIndex
     *   IO     → 最后一个 P-Core 之后（E-Core 区域）
     *
     * 非混合架构下：使用简单的轮询分配
     */
    uint32 GetRecommendedCore(ThreadCategory cat, uint32 workerIndex = 0);

} // namespace Threading
} // namespace Engine