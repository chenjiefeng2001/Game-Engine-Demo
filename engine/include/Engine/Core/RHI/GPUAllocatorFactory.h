#pragma once

/**
 * @file GPUAllocatorFactory.h
 * @brief 显存分配器工厂 — 根据后端类型创建对应的 IGPUMemoryAllocator 实现
 *
 * 设计原则：
 *   - 编译期/运行期切换后端（VMA / D3D12MA / Fallback 软件模拟）
 *   - 工厂只返回纯虚接口，调用方不感知具体实现
 *   - 与 IGraphicsFactory 解耦：GPU 内存管理器可独立创建，再注入到工厂
 *
 * 使用示例：
 * @code
 *   // 方式 1：由工厂自动选择后端
 *   auto allocator = CreateGPUMemoryAllocator(AllocatorBackend::Default);
 *
 *   // 方式 2：显式选择后端 + 配置
 *   GPUMemoryConfig config;
 *   config.deviceLocalBlockSize = 512 * 1024 * 1024; // 512MB 块
 *   auto allocator = CreateGPUMemoryAllocator(AllocatorBackend::VMA, config);
 * @endcode
 */

#include "Engine/Core/RHI/IGPUMemoryAllocator.h"
#include <memory>
#include <string>

namespace Engine {
namespace RHI {

    // ============================================================
    // 后端类型
    // ============================================================
    /**
     * @brief 分配器后端类型
     *
     *   Default   — 根据编译平台和可用后端自动选择最优实现
     *               优先级：VMA > D3D12MA > Fallback
     *   VMA       — VulkanMemoryAllocator 包装（生产级）
     *   D3D12MA   — D3D12MemoryAllocator 包装（生产级）
     *   Fallback  — 纯软件模拟（测试/Headless/CI 环境）
     */
    enum class AllocatorBackend : uint8 {
        Default  = 0,   ///< 自动选择最优后端
        VMA      = 1,   ///< VulkanMemoryAllocator（需要链接 VMA 库）
        D3D12MA  = 2,   ///< D3D12MemoryAllocator（需要链接 D3D12MA 库）
        Fallback = 3,   ///< 纯软件回退（无 GPU 依赖）
    };

    inline const char* AllocatorBackendName(AllocatorBackend backend) noexcept {
        switch (backend) {
            case AllocatorBackend::Default:  return "Auto";
            case AllocatorBackend::VMA:      return "VulkanMemoryAllocator";
            case AllocatorBackend::D3D12MA:  return "D3D12MemoryAllocator";
            case AllocatorBackend::Fallback: return "SoftwareFallback";
            default: return "Unknown";
        }
    }

    // ============================================================
    // 工厂函数声明（具体实现在各平台的 .cpp 中链接）
    // ============================================================

    /**
     * @brief 创建 GPU 显存分配器
     *
     * @param backend  后端类型
     * @param config   分配器配置（默认 = GPUMemoryConfig{}）
     * @return 已初始化（Initialize() 已调用）的分配器，失败返回 nullptr
     *
     * 调用方通过返回的 IGPUMemoryAllocator 接口操作，不感知具体后端。
     *
     * 创建流程：
     *   1. 根据 backend 实例化具体实现类
     *   2. 调用 Initialize(config) 完成内部初始化
     *   3. 若 Initialize 失败，返回 nullptr + 日志告警
     *
     * 注意：此函数可能返回 Fallback 实现（当目标后端不可用时），
     *       通过 allocator->GetConfig() 可检查实际使用的后端类型。
     */
    GPUMemoryAllocatorPtr CreateGPUMemoryAllocator(
        AllocatorBackend backend = AllocatorBackend::Default,
        const GPUMemoryConfig& config = GPUMemoryConfig{});

    /**
     * @brief 查询指定后端在当前平台是否可用
     * @param backend 后端类型
     * @return true 表示该后端已编译并可在当前运行时使用
     */
    bool IsBackendAvailable(AllocatorBackend backend) noexcept;

    /**
     * @brief 获取当前平台的默认后端
     *
     * 优先级：
     *   - 链接了 VMA → VMA
     *   - 链接了 D3D12MA → D3D12MA
     *   - 其他 → Fallback
     */
    AllocatorBackend GetDefaultBackend() noexcept;

} // namespace RHI
} // namespace Engine