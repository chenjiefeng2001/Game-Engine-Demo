#pragma once

/**
 * @file DeferredDeletionQueue.h
 * @brief 延迟销毁队列 — 保证 GPU 完成使用后再释放资源
 *
 * 设计要点：
 *   - 任何 RHI 资源的销毁请求不直接执行，而是加入队列
 *   - 每帧推进时，检查 Fence 是否已 signal
 *   - 只有在 GPU 完成该帧后（通常 2-3 帧延迟），才真正释放资源
 *   - 避免“资源还在 GPU 流水线上就被销毁”的 TDR 崩溃
 *
 * 使用方式：
 * @code
 *   m_DeletionQueue.Enqueue(frameIndex, [device, buffer]() {
 *       vmaDestroyBuffer(allocator, buffer, allocation);
 *   });
 *   // 或使用辅助函数：
 *   m_DeletionQueue.DestroyBuffer(frameIndex, allocator, buffer, allocation);
 * @endcode
 */

#include "Engine/Vulkan/VulkanCommon.h"
#include <queue>
#include <functional>
#include <vector>

namespace Engine {
namespace RHI {

class DeferredDeletionQueue {
public:
    DeferredDeletionQueue() noexcept = default;
    ~DeferredDeletionQueue() noexcept { FlushAll(); }

    DeferredDeletionQueue(const DeferredDeletionQueue&) = delete;
    DeferredDeletionQueue& operator=(const DeferredDeletionQueue&) = delete;

    /// 延迟帧数（GPU 完成该帧后再等1帧确保安全）
    static constexpr uint32_t kDeferFrames = kMaxFramesInFlight;

    // ════════════════════════════════════════════════════════
    // 通用延迟销毁
    // ════════════════════════════════════════════════════════

    /// 将销毁回调加入队列，关联到完成帧索引
    void Enqueue(uint64_t completedFrameIndex, std::function<void()>&& destructor) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pending.emplace_back(PendingDeletion{completedFrameIndex + kDeferFrames, std::move(destructor)});
    }

    /// 每帧调用：释放所有已满足延迟条件的资源
    void Tick(uint64_t currentFrameIndex) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        while (!m_Pending.empty() && m_Pending.front().frameIndex <= currentFrameIndex) {
            if (m_Pending.front().destructor) {
                m_Pending.front().destructor();
            }
            m_Pending.pop_front();
        }
    }

    /// 强制释放所有待销毁资源（设备销毁时调用）
    void FlushAll() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (auto& pending : m_Pending) {
            if (pending.destructor) {
                pending.destructor();
            }
        }
        m_Pending.clear();
    }

    /// 队列中待销毁的资源数
    size_t PendingCount() const noexcept {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Pending.size();
    }

    // ════════════════════════════════════════════════════════
    // 常用资源的辅助销毁函数
    // ════════════════════════════════════════════════════════

    void DestroyBuffer(uint64_t frameIndex, VmaAllocator allocator,
                       VkBuffer buffer, VmaAllocation allocation) {
        Enqueue(frameIndex, [allocator, buffer, allocation]() {
            vmaDestroyBuffer(allocator, buffer, allocation);
        });
    }

    void DestroyImage(uint64_t frameIndex, VmaAllocator allocator,
                      VkImage image, VmaAllocation allocation) {
        Enqueue(frameIndex, [allocator, image, allocation]() {
            vmaDestroyImage(allocator, image, allocation);
        });
    }

    void DestroyImageView(uint64_t frameIndex, VkDevice device, VkImageView view) {
        Enqueue(frameIndex, [device, view]() {
            vkDestroyImageView(device, view, nullptr);
        });
    }

    void DestroyPipeline(uint64_t frameIndex, VkDevice device, VkPipeline pipeline) {
        Enqueue(frameIndex, [device, pipeline]() {
            vkDestroyPipeline(device, pipeline, nullptr);
        });
    }

    void DestroyPipelineLayout(uint64_t frameIndex, VkDevice device, VkPipelineLayout layout) {
        Enqueue(frameIndex, [device, layout]() {
            vkDestroyPipelineLayout(device, layout, nullptr);
        });
    }

    void DestroyDescriptorPool(uint64_t frameIndex, VkDevice device, VkDescriptorPool pool) {
        Enqueue(frameIndex, [device, pool]() {
            vkDestroyDescriptorPool(device, pool, nullptr);
        });
    }

    void FreeCommandPool(uint64_t frameIndex, VkDevice device, VkCommandPool pool) {
        Enqueue(frameIndex, [device, pool]() {
            vkFreeCommandBuffers(device, pool, 0, nullptr);
            vkDestroyCommandPool(device, pool, nullptr);
        });
    }

private:
    struct PendingDeletion {
        uint64_t frameIndex;                    ///< 可以安全释放的帧索引
        std::function<void()> destructor;       ///< 实际释放资源的 lambda
    };

    mutable std::mutex m_Mutex;
    std::deque<PendingDeletion> m_Pending;      ///< 按帧索引排序的待销毁队列
};

} // namespace RHI
} // namespace Engine