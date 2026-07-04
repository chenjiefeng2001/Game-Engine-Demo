#pragma once

/**
 * @file IGPUMemoryAllocator.h
 * @brief RHI 显存分配器纯虚接口 — 引擎层只依赖此接口，不接触具体 API
 *
 * 设计级别：与 VulkanMemoryAllocator / D3D12MA 对等
 *
 * 核心职责：
 *   1. 资源创建 + 内存绑定一步完成（CreateBuffer / CreateImage）
 *   2. 纯内存分配（Allocate），供外部 API 对象绑定
 *   3. 子分配策略（Bump / FrameTransient / Pool / Buddy）
 *   4. CPU 端一致性控制（Map/Unmap/Flush/Invalidate）
 *   5. GPU 设备地址支持（Vulkan 1.2+ / D3D12 GPU-VA）
 *
 * 两种使用模式：
 *
 *   模式 A（推荐）：分配器全权管理
 *   @code
 *     BufferDesc desc{ 64 * 1024, USAGE_VERTEX };
 *     auto alloc = allocator->CreateBuffer(desc, MemoryUsage::GPU_Only,
 *                                          AllocationStrategy::Pool,
 *                                          GPUResourceType::Buffer_Static, "VBO");
 *     // alloc.apiResource 直接是 VkBuffer / ID3D12Resource / GLuint
 *   @endcode
 *
 *   模式 B：外部创建 API 对象，向分配器申请内存
 *   @code
 *     VkBuffer buffer = CreateVkBuffer(...);            // 外部创建
 *     VkMemoryRequirements reqs;                        // ← 驱动查询
 *     vkGetBufferMemoryRequirements(device, buffer, &reqs);
 *     auto alloc = allocator->Allocate(reqs.size, reqs.alignment,
 *                                      MemoryUsage::GPU_Only, reqs.memoryTypeBits,
 *                                      AllocationStrategy::Buddy,
 *                                      ResourceNature::Linear, ...);
 *     vkBindBufferMemory(device, buffer, alloc.apiMemoryHandle, alloc.offset);
 *   @endcode
 *
 * 线程安全：
 *   分配/释放操作本身是线程安全的（内部互斥）。
 *   Map/Flush/Invalidate 必须在正确的线程调用（与具体 API 上下文绑定）。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MemoryTypes.h"
#include "Engine/Core/RHI/GPUAllocation.h"
#include "Engine/Core/RHI/GPUMemoryBlock.h"
#include <functional>
#include <string>
#include <vector>

namespace Engine {
namespace RHI {

    // ============================================================
    // 分配器配置
    // ============================================================
    struct GPUMemoryConfig {
        /** DeviceLocal 默认块大小（如 256MB） */
        uint64  deviceLocalBlockSize = 256ULL * 1024 * 1024;

        /** HostVisible 默认块大小（如 64MB） */
        uint64  hostVisibleBlockSize = 64ULL * 1024 * 1024;

        /** GPU 设备总预算上限（0 = 不限制） */
        uint64  maxDeviceBudget = 0;

        /** 飞行帧数（FrameTransient 策略使用，决定环形缓冲分区数） */
        uint32  framesInFlight = 2;

        /** 每帧瞬态内存预算上限（0 = 无限制） */
        uint64  transientBudgetPerFrame = 64ULL * 1024 * 1024;

        /** 是否启用 GPU 设备地址 */
        bool    enableDeviceAddress = false;

        /** 是否启用碎片整理 */
        bool    enableDefrag = true;

        /** 碎片整理触发阈值 */
        float   defragThreshold = 0.25f;

        /** Buffer 和 Image 的最小隔离粒度（字节，0 = 使用物理设备查询值） */
        uint64  bufferImageGranularity = 0;

        /** 是否在每次分配时追踪调用栈 */
        bool    enableCallstackTracking = false;

        /** 后台 Trimming 频率（帧） */
        uint32  trimFrameInterval = 60;

        /** Trim 时保留的最小空闲块数 */
        uint32  minFreeBlocks = 1;

        /** 预置资源池大小 */
        struct PoolPreset {
            GPUResourceType type;
            uint64          slotSize;
            uint32          initialSlots;
        };
        std::vector<PoolPreset> poolPresets;
    };

    // ============================================================
    // 全局统计
    // ============================================================
    struct MemoryStats {
        uint64  totalAllocated      = 0;
        uint64  totalUsed           = 0;
        uint64  totalFree           = 0;
        uint64  peakAllocated       = 0;
        uint64  peakUsed            = 0;
        uint32  blockCount          = 0;
        uint32  peakBlockCount      = 0;
        uint32  fullyFreeBlockCount = 0;
        uint64  allocationCount     = 0;
        uint64  peakAllocationCount = 0;
        uint64  totalSubAllocations = 0;
        uint64  failedAllocations   = 0;
        float   internalFragmentation = 0.0f;

        // ── 按资源本性分类 ──
        uint64  linearAllocations   = 0;   ///< Buffer 类分配数
        uint64  imageAllocations    = 0;   ///< Image 类分配数
        uint64  linearBytes         = 0;
        uint64  imageBytes          = 0;
    };

    // ============================================================
    // IGPUMemoryAllocator 主接口
    // ============================================================
    class IGPUMemoryAllocator {
    public:
        IGPUMemoryAllocator() = default;
        virtual ~IGPUMemoryAllocator() = default;

        IGPUMemoryAllocator(const IGPUMemoryAllocator&) = delete;
        IGPUMemoryAllocator& operator=(const IGPUMemoryAllocator&) = delete;
        IGPUMemoryAllocator(IGPUMemoryAllocator&&) = default;
        IGPUMemoryAllocator& operator=(IGPUMemoryAllocator&&) = default;

        // ── 初始化/关闭 ──

        virtual bool Initialize(const GPUMemoryConfig& config) = 0;
        virtual void Shutdown() = 0;
        virtual bool IsReady() const noexcept = 0;

        // ══════════════════════════════════════════════════════
        // 模式 A：分配器全权管理（推荐）
        // ══════════════════════════════════════════════════════

        /**
         * @brief 创建 Buffer 并分配绑定内存（一步完成）
         *
         * @param desc         Buffer 创建描述
         * @param usage        内存用途
         * @param strategy     分配策略（默认 Buddy）
         * @param resourceType 资源子类型（用于统计分类）
         * @param debugTag     调试标签
         * @return 包含 apiResource + apiMemoryHandle + mappedPtr 的完整分配
         *
         * 后端实现流程（以 Vulkan 为例）：
         *   1. vkCreateBuffer(desc.size, desc.usageFlags, ...) → VkBuffer
         *   2. vkGetBufferMemoryRequirements(buffer) → size/alignment/memoryTypeBits
         *   3. 内部 Allocate(reqs.size, reqs.alignment, usage, reqs.memoryTypeBits, ...)
         *   4. vkBindBufferMemory(buffer, memory, offset)
         *   5. 返回 GPUAllocation{apiResource = buffer, apiMemoryHandle = memory, ...}
         */
        virtual GPUAllocation CreateBuffer(
            const BufferDesc&   desc,
            MemoryUsage         usage          = MemoryUsage::GPU_Only,
            AllocationStrategy  strategy       = AllocationStrategy::Buddy,
            GPUResourceType     resourceType   = GPUResourceType::Buffer_Static,
            const char*         debugTag       = nullptr) = 0;

        /**
         * @brief 创建 Image 并分配绑定内存（一步完成）
         *
         * @param desc         Image 创建描述
         * @param usage        内存用途
         * @param strategy     分配策略（默认 Buddy）
         * @param resourceType 资源子类型（用于统计分类）
         * @param debugTag     调试标签
         * @return 包含 apiResource + apiMemoryHandle 的完整分配
         *
         * 后端实现流程：
         *   1. vkCreateImage(desc.width, desc.height, desc.format, ...) → VkImage
         *   2. vkGetImageMemoryRequirements(image) → size/alignment/memoryTypeBits
         *   3. 内部分配（考虑 linear/image 隔离）
         *   4. vkBindImageMemory(image, memory, offset)
         */
        virtual GPUAllocation CreateImage(
            const ImageDesc&    desc,
            MemoryUsage         usage          = MemoryUsage::GPU_Only,
            AllocationStrategy  strategy       = AllocationStrategy::Buddy,
            GPUResourceType     resourceType   = GPUResourceType::Texture_2D,
            const char*         debugTag       = nullptr) = 0;

        // ══════════════════════════════════════════════════════
        // 模式 B：外部拥有 API 对象，仅申请内存
        // ══════════════════════════════════════════════════════

        /**
         * @brief 分配一块显存（供外部 API 对象绑定）
         *
         * @param size          请求字节数
         * @param alignment     对齐要求（0 = 使用默认值）
         * @param usage         内存用途
         * @param memoryTypeBits 硬件内存类型掩码（Vulkan: reqs.memoryTypeBits，
         *                       D3D12: 0 = 自动选择，OpenGL: 忽略）
         * @param strategy      分配策略
         * @param nature        资源本性（Linear/Image，用于隔离混用冲突）
         * @param resourceType  资源子类型
         * @param debugTag      调试标签
         * @return GPUAllocation（不包含 apiResource，调用方负责后续 Bind）
         */
        virtual GPUAllocation Allocate(
            uint64              size,
            uint64              alignment,
            MemoryUsage         usage            = MemoryUsage::GPU_Only,
            uint32              memoryTypeBits   = 0,
            AllocationStrategy  strategy         = AllocationStrategy::Buddy,
            ResourceNature      nature           = ResourceNature::Unknown,
            GPUResourceType     resourceType     = GPUResourceType::Unknown,
            const char*         debugTag         = nullptr) = 0;

        // ── 释放 ──

        /**
         * @brief 释放一个分配（含对应的 API 资源对象）
         *
         * @param allocation 由 Allocate/CreateBuffer/CreateImage 返回的分配
         *
         * 若 allocation.apiResource != nullptr，后端会自动销毁对应的
         * API 资源对象（vkDestroyBuffer / ID3D12Resource::Release / glDeleteBuffers）。
         * 调用方无需额外清理。
         */
        virtual void Deallocate(GPUAllocation& allocation) = 0;

        // ── 便利方法 ──

        GPUAllocation AllocateStatic(uint64 size, uint64 alignment = 0,
                                     GPUResourceType resourceType = GPUResourceType::Unknown,
                                     const char* debugTag = nullptr) {
            return Allocate(size, alignment, MemoryUsage::GPU_Only, 0,
                           AllocationStrategy::Buddy,
                           GPUResourceTypeToNature(resourceType),
                           resourceType, debugTag);
        }

        GPUAllocation AllocateDynamic(uint64 size, uint64 alignment = 0,
                                      GPUResourceType resourceType = GPUResourceType::Unknown,
                                      const char* debugTag = nullptr) {
            return Allocate(size, alignment, MemoryUsage::CPU_To_GPU, 0,
                           AllocationStrategy::FrameTransient,
                           GPUResourceTypeToNature(resourceType),
                           resourceType, debugTag);
        }

        GPUAllocation AllocateStaging(uint64 size, uint64 alignment = 0,
                                      GPUResourceType resourceType = GPUResourceType::Unknown,
                                      const char* debugTag = nullptr) {
            return Allocate(size, alignment, MemoryUsage::CPU_Only, 0,
                           AllocationStrategy::Bump,
                           GPUResourceTypeToNature(resourceType),
                           resourceType, debugTag);
        }

        GPUAllocation AllocateReadback(uint64 size, uint64 alignment = 0,
                                       GPUResourceType resourceType = GPUResourceType::Unknown,
                                       const char* debugTag = nullptr) {
            return Allocate(size, alignment, MemoryUsage::GPU_To_CPU, 0,
                           AllocationStrategy::Bump,
                           GPUResourceTypeToNature(resourceType),
                           resourceType, debugTag);
        }

        // ── 一致性控制 ──

        virtual void FlushAllocation(const GPUAllocation& allocation,
                                     uint64 offset = 0, uint64 size = 0) = 0;
        virtual void InvalidateAllocation(const GPUAllocation& allocation,
                                          uint64 offset = 0, uint64 size = 0) = 0;

        // ── 内存块管理 ──

        virtual void Defragment() = 0;
        virtual void Trim() = 0;

        /**
         * @brief 每帧末尾调用
         *
         * 处理：
         *   - FrameTransient 策略的环形缓冲指针推进
         *   - 周期性 Trim 调度
         */
        virtual void EndFrame() = 0;

        // ── 统计 ──

        virtual MemoryStats GetStats() const = 0;
        virtual std::string DumpStats() const = 0;
        virtual void LogStats() const = 0;

        // ── OOM 处理 ──

        using OOMCallback = void(*)(uint64 requestedSize, MemoryUsage usage,
                                    const char* debugTag, void* userData);
        virtual void SetOOMCallback(OOMCallback callback, void* userData) = 0;

        // ── 诊断 ──

        virtual std::vector<MemoryBlockStats> GetPerBlockStats() const = 0;
        virtual void UpdateConfig(const GPUMemoryConfig& config) = 0;
        virtual const GPUMemoryConfig& GetConfig() const noexcept = 0;
    };

    using GPUMemoryAllocatorPtr = std::unique_ptr<IGPUMemoryAllocator>;

} // namespace RHI
} // namespace Engine