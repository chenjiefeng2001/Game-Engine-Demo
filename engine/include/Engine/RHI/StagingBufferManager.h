#pragma once

/**
 * @file StagingBufferManager.h
 * @brief 全局环形上传缓冲管理器 — 异步数据上传到 GPU
 *
 * 设计：
 *   - 每帧分配一块环形 buffer（Upload Heap）
 *   - 帧结束后偏移重置（不需要逐资源释放）
 *   - 支持 CPU→GPU 数据拷贝（Buffer/Texture 上传）
 *   - 支持 Fence 同步：确保 GPU 完成读取前数据有效
 *
 * 线程安全：否（渲染线程独占）
 */

#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/Log.h"
#include <cstring>
#include <vector>

namespace Engine {
namespace RHI {

    // ============================================================
    // StagingAllocation — 单次上传分配的 CPU/GPU 映射
    // ============================================================
    struct StagingAllocation {
        void*    cpuPtr      = nullptr;   ///< CPU 可写指针
        uint64_t gpuAddress  = 0;          ///< GPU 虚拟地址（Buffer 上传用）
        uint64_t offset      = 0;          ///< 在 staging buffer 中的偏移
        uint64_t size        = 0;
        bool     valid       = false;
    };

    // ============================================================
    // StagingBufferManager — 环形上传缓冲管理器
    // ============================================================
    class StagingBufferManager {
    public:
        StagingBufferManager() = default;
        ~StagingBufferManager();

        StagingBufferManager(const StagingBufferManager&) = delete;
        StagingBufferManager& operator=(const StagingBufferManager&) = delete;

        /**
         * @brief 初始化上传管理器
         * 
         * @param device     RHI 设备
         * @param bufferSize 每帧 staging buffer 大小（默认 64MB）
         * @return true      成功
         */
        bool Initialize(IRHIDevice& device, uint64_t bufferSize = 64 * 1024 * 1024);

        /** @brief 是否已初始化 */
        bool IsInitialized() const noexcept { return m_Initialized; }

        /**
         * @brief 在 staging buffer 中分配一块空间
         * 
         * @param size       需要的字节数
         * @param alignment  对齐要求（默认 256）
         * @return StagingAllocation
         */
        StagingAllocation Allocate(uint64_t size, uint64_t alignment = 256);

        /**
         * @brief 将 CPU 数据拷贝到 staging allocation
         * 
         * @param dst  staging allocation（从 Allocate 获得）
         * @param src  CPU 源数据指针
         * @param size 拷贝字节数
         */
        void CopyToStaging(const StagingAllocation& dst, const void* src, uint64_t size);

        /**
         * @brief 帧结束 — 重置帧内偏移
         * 
         * 注意：调用此方法前必须确保 GPU 已完成上一帧的读取。
         * 可以通过 IRHICommandQueue::WaitIdle() 或 Fence 同步。
         */
        void EndFrame();

        /** @brief 释放 GPU 资源 */
        void Shutdown();

        // ── 统计 ──

        uint64_t GetBufferSize() const noexcept { return m_BufferSize; }
        uint64_t GetUsedSize() const noexcept { return m_CurrentOffset; }
        float    GetUtilization() const noexcept {
            return m_BufferSize > 0 ? (float)m_CurrentOffset / (float)m_BufferSize : 0.0f;
        }

    private:
        // ── GPU 资源 ──
        IRHIBuffer* m_StagingBuffer = nullptr;

        // ── 映射指针 ──
        void* m_MappedPtr = nullptr;

        // ── 帧内分配状态 ──
        uint64_t m_CurrentOffset = 0;
        uint64_t m_BufferSize    = 0;
        bool     m_Initialized   = false;
    };

} // namespace RHI
} // namespace Engine