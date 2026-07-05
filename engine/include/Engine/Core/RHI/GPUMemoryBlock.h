#pragma once

/**
 * @file GPUMemoryBlock.h
 * @brief GPU 内存块抽象 — 封装后端 API 的一块连续显存
 *
 * 设计理念：
 *   - 纯虚接口，不依赖具体 API（Vulkan VkDeviceMemory / D3D12 ID3D12Heap）
 *   - 每个块对应一次 OS 级 GPU 内存分配，大小通常为 64MB / 256MB
 *   - IGPUMemoryAllocator 在块内部进行子分配
 *   - 块的生命周期由 allocator 通过 unique_ptr 管理
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MemoryTypes.h"
#include <memory>

namespace Engine {
namespace RHI {

    // ============================================================
    // 块统计（只读，供上层查询）
    // ============================================================
    struct MemoryBlockStats {
        uint64          totalBytes      = 0;   ///< 块总大小（字节）
        uint64          allocatedBytes  = 0;   ///< 已分配字节（含内部对齐/碎片）
        uint64          freeBytes       = 0;   ///< 空闲字节（连续空闲区域之和）
        uint32          allocationCount = 0;   ///< 当前活跃分配数
        uint32          maxAllocations  = 0;   ///< 块内同时存在的最大分配数
        MemoryProperty  properties      = MemoryProperty::DeviceLocal;
        MemoryUsage     usage           = MemoryUsage::GPU_Only;
        bool            isFullyFree     = false; ///< 完全空闲（可安全回收）
    };

    // ============================================================
    // GPUMemoryBlock 纯虚接口
    // ============================================================
    class GPUMemoryBlock {
    public:
        virtual ~GPUMemoryBlock() = default;

        // ── 属性查询 ──

        /** 块总大小（字节） */
        virtual uint64 GetSize() const noexcept = 0;

        /** 内存属性 */
        virtual MemoryProperty GetProperties() const noexcept = 0;

        /** 内存用途 */
        virtual MemoryUsage GetUsage() const noexcept = 0;

        // ── CPU 映射 ──

        /**
         * @brief 映射块内区域到 CPU 地址空间
         * @param offset  块内偏移
         * @param size    映射大小（0 = 映射全部从 offset 到块尾）
         * @return CPU 可访问指针，不支持映射时返回 nullptr
         *
         * 仅 HostVisible 的块支持此操作。
         * 调用前应先检查 HasProperty(properties, MemoryProperty::HostVisible)。
         * 对于 HostCoherent 内存，写入立即可见 GPU；其他情况需手动 Flush。
         */
        virtual void* Map(uint64 offset, uint64 size) = 0;

        /** 解除 CPU 映射 */
        virtual void Unmap() = 0;

        /** 是否已映射 */
        virtual bool IsMapped() const noexcept = 0;

        // ── 一致性控制（非 HostCoherent 时需要） ──

        /**
         * @brief 刷新 CPU 写入到 GPU 可见
         * @param offset  块内偏移
         * @param size    刷新范围
         *
         * 对应 Vulkan: vkFlushMappedMemoryRanges
         *       D3D12: 无需操作（UMA 架构自动可见）
         */
        virtual void FlushCPUWrite(uint64 offset, uint64 size) = 0;

        /**
         * @brief 使 GPU 写入对 CPU 可见
         * @param offset  块内偏移
         * @param size    无效化范围
         *
         * 对应 Vulkan: vkInvalidateMappedMemoryRanges
         *       D3D12: 无需操作
         */
        virtual void InvalidateGPUWrite(uint64 offset, uint64 size) = 0;

        // ── 设备地址（Vulkan 1.2+ / D3D12 GPU-VA） ──

        /**
         * @brief 获取块的 GPU 设备起始地址
         * @return 设备基地址，0 表示不支持
         */
        virtual uint64 GetDeviceBaseAddr() const noexcept = 0;

        /** GPU 设备地址是否可用 */
        virtual bool SupportsDeviceAddress() const noexcept = 0;

        // ── 统计 ──

        /** 获取当前的子分配统计（由 allocator 更新） */
        virtual MemoryBlockStats GetStats() const noexcept = 0;

        /** 调试名称（后端可重写以包含具体 API 句柄信息） */
        virtual const char* GetDebugName() const noexcept = 0;
    };

    /** GPUMemoryBlock 的所有权指针类型 */
    using GPUMemoryBlockPtr = std::unique_ptr<GPUMemoryBlock>;

} // namespace RHI
} // namespace Engine