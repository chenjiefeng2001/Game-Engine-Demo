#pragma once

/**
 * @file DynamicUBOAllocator.h
 * @brief 动态 Uniform Buffer 分配器 — 每帧线性分配 + GPU 同步
 *
 * 设计目标：
 *   消除每物体每帧的单独 UBO 上传 API 调用。
 *   所有 MaterialInstance 的参数数据集中到一个大的 GPU Buffer 中，
 *   通过 Dynamic Offset 切换材质参数。
 *
 * 分配策略：
 *   - 预先分配一个 16MB 的 GPU 缓冲（CPU-visible, GPU-readable）
 *   - 每帧使用线性 bump allocator 分配
 *   - 分配的最小单元对齐到 256 字节（Vulkan 动态 UBO 的对齐要求）
 *   - 帧结束后重置指针（Frame-cycled double/triple buffering）
 *
 * 使用方式：
 * @code
 *   DynamicUBOAllocator allocator;
 *   allocator.Initialize(device, 16 * 1024 * 1024);   // 16MB
 *
 *   // 每材质实例分配一段空间
 *   auto allocation = allocator.Allocate(sizeof(PBRParams), 256);
 *   memcpy(allocation.cpuPtr, &params, sizeof(PBRParams));
 *   cmd.SetDynamicUBO(0, allocation.gpuOffset);         // 只提交偏移量
 *
 *   allocator.EndFrame(frameIndex);                     // 帧结束翻转
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/IRHICommandList.h"

namespace Engine {
namespace Rendering {

    // ============================================================
    // 分配结果
    // ============================================================
    struct UBOAllocation {
        void*    cpuPtr    = nullptr;   ///< 映射后的 CPU 指针
        uint64_t gpuOffset = 0;         ///< 在 GPU Buffer 中的偏移
        uint64_t size      = 0;         ///< 实际分配大小
        bool     valid     = false;
    };

    // ============================================================
    // DynamicUBOAllocator
    // ============================================================
    /**
     * @brief 动态 UBO 分配器 — 线性 bump allocator
     *
     * 线程安全：分配在单线程（渲染线程）进行，无需锁。
     * Buffer 生命周期由 allocator 管理。
     */
    class DynamicUBOAllocator {
    public:
        DynamicUBOAllocator() = default;
        ~DynamicUBOAllocator();

        // 禁止拷贝
        DynamicUBOAllocator(const DynamicUBOAllocator&) = delete;
        DynamicUBOAllocator& operator=(const DynamicUBOAllocator&) = delete;

        // 允许移动
        DynamicUBOAllocator(DynamicUBOAllocator&&) noexcept;
        DynamicUBOAllocator& operator=(DynamicUBOAllocator&&) noexcept;

        /**
         * @brief 初始化分配器
         *
         * @param device       RHI 设备
         * @param bufferSize   总缓冲区大小（默认 16MB）
         * @param alignment    对齐（默认 256 字节，Vulkan minUniformBufferOffsetAlignment）
         * @return true 表示成功
         */
        bool Initialize(RHI::IRHIDevice& device,
                        uint64_t bufferSize = 16 * 1024 * 1024,
                        uint64_t alignment = 256);

        /** 是否已初始化 */
        bool IsInitialized() const noexcept { return m_Initialized; }

        /**
         * @brief 分配一段 UBO 空间
         *
         * @param size 请求的字节数（会被对齐到 m_Alignment）
         * @return 分配结果
         *
         * 此分配不涉及任何 GPU 同步操作，仅移动指针。
         * 若空间不足→分配失败（valid=false），log 警告。
         */
        UBOAllocation Allocate(uint64_t size);

        /**
         * @brief 帧结束 — 翻转 ring buffer 并重置分配指针
         *
         * @param frameIndex 帧索引（用于跟踪 GPU 完成）
         */
        void EndFrame(uint64_t frameIndex);

        /**
         * @brief 释放 GPU 资源
         */
        void Shutdown();

        // ── Buffer 访问 ──

        /** 获取底层 GPU Buffer 指针 */
        RHI::IRHIBuffer* GetBuffer() const noexcept { return m_Buffer; }

        // ── 统计 ──
        uint64_t GetTotalSize() const noexcept { return m_BufferSize; }
        uint64_t GetAllocated() const noexcept { return m_CurrentOffset; }
        uint64_t GetAlignment() const noexcept { return m_Alignment; }
        float    GetUtilization() const noexcept {
            return m_BufferSize > 0 ? (float)m_CurrentOffset / (float)m_BufferSize : 0.0f;
        }

    private:
        // ── GPU 资源 ──
        RHI::IRHIBuffer* m_Buffer  = nullptr;
        void*            m_MappedPtr = nullptr;   ///< 持久映射的 CPU 指针

        // ── 分配状态 ──
        uint64_t m_BufferSize   = 0;
        uint64_t m_Alignment    = 256;
        uint64_t m_CurrentOffset = 0;
        bool     m_Initialized  = false;

        // ── Fence 同步（用于 GPU 帧同步） ──
        uint64_t m_CurrentFrame = 0;
    };

} // namespace Rendering
} // namespace Engine