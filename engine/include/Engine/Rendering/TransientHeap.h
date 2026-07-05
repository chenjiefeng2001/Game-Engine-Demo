#pragma once

/**
 * @file TransientHeap.h
 * @brief 瞬态显存池 — 基于生命周期分析的线性分配器
 *
 * 设计目标：
 *   同一帧内，生命周期不重叠的多个 RenderGraph 资源共享同一段显存。
 *   帧结束后整个池重置（不需要逐资源释放）。
 *
 * 分配策略：
 *   - 预先分配一大块 GPU 显存（默认 256MB，DEVICE_LOCAL）
 *   - RenderGraph Compile() 后分析所有资源的 firstUse/lastUse
 *   - 对生命周期不重叠的资源分配到同一段物理偏移
 *   - 在重用资源边界自动插入 Aliasing Barrier
 *
 * 线程安全：非线程安全（在渲染线程/帧上下文内使用）
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/IRHIDevice.h"

namespace Engine {
namespace Rendering {

    // ============================================================
    // 瞬态资源分配请求
    // ============================================================
    struct TransientAllocRequest {
        uint64_t    size  = 0;          ///< 字节大小
        uint64_t    alignment = 256;    ///< 对齐
        RHI::Format format = RHI::Format::Unknown;
        bool        isTexture = false;  ///< true=Image, false=Buffer
        uint32_t    width  = 0;
        uint32_t    height = 0;
        uint32_t    arrayLayers = 1;
        uint32_t    mipLevels   = 1;
        uint32_t    firstUsePass = UINT32_MAX;
        uint32_t    lastUsePass  = 0;
    };

    // ============================================================
    // 分配结果
    // ============================================================
    struct TransientAllocation {
        void*              cpuPtr    = nullptr;   ///< 映射指针（仅 upload heap）
        uint64_t           offset    = 0;          ///< 在 heap 中的起始偏移
        RHI::IRHIBuffer*   buffer    = nullptr;    ///< 底层 Buffer 句柄
        uint64_t           bufferSize = 0;
        bool               valid     = false;
    };

    // ============================================================
    // 瞬态内存块的生命周期区间
    // ============================================================
    struct LifetimeRange {
        uint32_t firstUse;
        uint32_t lastUse;
        uint64_t offset;               ///< 在 heap 中的偏移
        uint64_t size;                 ///< 对齐后的大小
        uint32_t resourceIndex;        ///< 指向 TransientAllocRequest 索引
        bool     active = false;
    };

    // ============================================================
    // TransientHeap
    // ============================================================
    class TransientHeap {
    public:
        TransientHeap() = default;
        ~TransientHeap();

        TransientHeap(const TransientHeap&) = delete;
        TransientHeap& operator=(const TransientHeap&) = delete;

        // 允许移动
        TransientHeap(TransientHeap&&) noexcept;
        TransientHeap& operator=(TransientHeap&&) noexcept;

        /**
         * @brief 初始化瞬态堆
         *
         * @param device      RHI 设备
         * @param heapSize    堆总大小（默认 256MB）
         * @return true 表示成功
         */
        bool Initialize(RHI::IRHIDevice& device,
                        uint64_t heapSize = 256 * 1024 * 1024);

        /** 是否已初始化 */
        bool IsInitialized() const noexcept { return m_Initialized; }

        /**
         * @brief 注册一个瞬态资源请求
         *
         * @param request 资源描述
         * @return 此请求的索引（用于后续查询）
         */
        uint32_t RegisterRequest(const TransientAllocRequest& request);

        /**
         * @brief 分析所有已注册请求的生命周期 → 分配物理偏移
         *
         * 使用 First-Fit 分配算法对生命周期不重叠的资源进行复用。
         * 在 Compile() 之后调用。
         *
         * @return true 表示所有资源分配成功
         */
        bool Compile();

        /**
         * @brief 获取已编译的分配结果
         *
         * @param requestIndex RegisterRequest() 返回的索引
         * @return 分配结果
         */
        TransientAllocation GetAllocation(uint32_t requestIndex) const;

        /**
         * @brief 帧结束 — 重置分配器
         *
         * 数据仍然存在于显存中，但下一帧不再保证有效。
         */
        void Reset();

        /**
         * @brief 释放 GPU 资源
         */
        void Shutdown();

        // ── 统计 ──
        uint64_t GetHeapSize() const noexcept { return m_HeapSize; }
        uint64_t GetUsedSize() const noexcept { return m_UsedSize; }
        uint32_t GetRequestCount() const noexcept { return static_cast<uint32_t>(m_Requests.size()); }
        float    GetUtilization() const noexcept {
            return m_HeapSize > 0 ? (float)m_UsedSize / (float)m_HeapSize : 0.0f;
        }

    private:
        // ── GPU 资源 ──
        RHI::IRHIBuffer* m_HeapBuffer  = nullptr;  ///< 整个堆的 GPU Buffer
        void*            m_MappedPtr   = nullptr;   ///< 持久映射指针（upload heap 专用）

        // ── 堆配置 ──
        uint64_t m_HeapSize    = 0;
        uint64_t m_UsedSize    = 0;
        bool     m_Initialized = false;
        bool     m_Compiled    = false;

        // ── 资源请求 ──
        std::vector<TransientAllocRequest> m_Requests;
        std::vector<LifetimeRange>          m_Lifetimes;
        std::vector<TransientAllocation>    m_Allocations;

        // ── 分配算法 ──

        /**
         * @brief 检查两个生命周期区间是否重叠
         */
        static bool Overlaps(const LifetimeRange& a, const LifetimeRange& b) noexcept {
            return a.firstUse <= b.lastUse && b.firstUse <= a.lastUse;
        }

        /**
         * @brief 对齐大小
         */
        uint64_t AlignUp(uint64_t size, uint64_t alignment) const noexcept {
            return (size + alignment - 1) & ~(alignment - 1);
        }
    };

} // namespace Rendering
} // namespace Engine