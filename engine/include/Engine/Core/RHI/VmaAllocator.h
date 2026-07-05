#pragma once

/**
 * @file VmaAllocator.h
 * @brief VulkanMemoryAllocator (VMA) 的引擎分配器封装 — 实现 IGPUMemoryAllocator 接口
 *
 * 设计理念：
 *   - 不自己实现 Buddy/Bump 分配器，直接委托给 VMA（业界最优化实现）
 *   - VMA 内部已包含：线性分配、池分配、碎片整理、统计
 *   - 仅暴露 IGPUMemoryAllocator 统一接口
 *
 * VMA 特性映射：
 *   - VMA_MEMORY_USAGE_GPU_ONLY    → MemoryUsage::GPU_Only
 *   - VMA_MEMORY_USAGE_CPU_TO_GPU  → MemoryUsage::CPU_To_GPU
 *   - VMA_MEMORY_USAGE_GPU_TO_CPU  → MemoryUsage::GPU_To_CPU
 *   - VMA_MEMORY_USAGE_CPU_ONLY    → MemoryUsage::CPU_Only
 *
 * 使用方式：
 * @code
 *   VmaAllocator allocator;
 *   allocator.Initialize(device, physicalDevice, instance);
 *   auto alloc = allocator->Allocate(1024, 16, MemoryUsage::GPU_Only, 0,
 *                                     AllocationStrategy::Buddy,
 *                                     ResourceNature::Linear,
 *                                     GPUResourceType::Buffer_Static, "VBO");
 *   // alloc.apiMemoryHandle = VkDeviceMemory
 *   // alloc.apiResource     = VkBuffer (由 CreateBuffer 创建)
 * @endcode
 *
 * 注意：此文件需要链接 Vulkan SDK + VMA 库。
 * 在未链接 VMA 时退回到 FallbackAllocator。
 */

#include "Engine/Core/RHI/IGPUMemoryAllocator.h"
#include <memory>

namespace Engine {
namespace RHI {

    // ══════════════════════════════════════════════════════
    // VMA 分配器实现 — IGPUMemoryAllocator 接口
    // ══════════════════════════════════════════════════════
    /**
     * @brief VulkanMemoryAllocator 包装器
     *
     * 生命周期：
     *   1. 调用 Initialize() 创建 VMA 实例
     *   2. Allocate/CreateBuffer/CreateImage 使用 VMA 分配
     *   3. Deallocate 释放回 VMA
     *   4. Shutdown() 销毁 VMA 实例
     */
    class VmaAllocator final : public IGPUMemoryAllocator {
    public:
        VmaAllocator();
        ~VmaAllocator() override;

        // ── IGPUMemoryAllocator ──
        bool Initialize(const GPUMemoryConfig& config) override;
        void Shutdown() override;
        bool IsReady() const noexcept override;

        GPUAllocation CreateBuffer(
            const BufferDesc& desc, MemoryUsage usage,
            AllocationStrategy strategy, GPUResourceType resourceType,
            const char* debugTag) override;

        GPUAllocation CreateImage(
            const ImageDesc& desc, MemoryUsage usage,
            AllocationStrategy strategy, GPUResourceType resourceType,
            const char* debugTag) override;

        GPUAllocation Allocate(
            uint64 size, uint64 alignment, MemoryUsage usage,
            uint32 memoryTypeBits, AllocationStrategy strategy,
            ResourceNature nature, GPUResourceType resourceType,
            const char* debugTag) override;

        void Deallocate(GPUAllocation& alloc) override;

        void FlushAllocation(const GPUAllocation& alloc,
                            uint64 offset = 0, uint64 size = 0) override;
        void InvalidateAllocation(const GPUAllocation& alloc,
                                uint64 offset = 0, uint64 size = 0) override;

        void Defragment() override;
        void Trim() override;
        void EndFrame() override;

        MemoryStats GetStats() const override;
        std::string DumpStats() const override;
        void LogStats() const override;

        void SetOOMCallback(OOMCallback cb, void* user) override;
        std::vector<MemoryBlockStats> GetPerBlockStats() const override;
        void UpdateConfig(const GPUMemoryConfig& config) override;
        const GPUMemoryConfig& GetConfig() const noexcept override;

        // ── VMA 专有 ──
        bool InitializeWithVulkan(void* vkDevice,
                                  void* vkPhysicalDevice,
                                  void* vkInstance,
                                  uint32_t vulkanApiVersion);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    /** 检查 VMA 库是否可用 (编译时链接了 VMA) */
    bool HasVmaSupport() noexcept;

    /** 创建 VMA 分配器（仅在 VMA 不可用回退时返回 nullptr） */
    std::unique_ptr<IGPUMemoryAllocator> CreateVmaAllocator();

} // namespace RHI
} // namespace Engine