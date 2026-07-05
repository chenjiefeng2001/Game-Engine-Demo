#pragma once

/**
 * @file SyncManager.h
 * @brief Vulkan 同步管理器 — 三重缓冲 Fence/Semaphore 封装
 *
 * 解决的核心问题：
 *   CPU 提交命令后 GPU 异步执行。如果不等待 GPU 完成就重用资源（UBO/Descriptor），
 *   会导致随机花屏或驱动重置（TDR）。
 *
 * 设计要点：
 *   - kMaxFramesInFlight = 3 三重缓冲
 *   - WaitForFrame() — CPU 等待当前帧的 Fence（阻塞）
 *   - SignalFrame() — 提交后将 Fence 关联到当前帧
 *   - 外部负责 vkQueueSubmit 时传入 fence
 */

#include "Engine/Vulkan/VulkanCommon.h"
#include <cstdio>

namespace Engine {
namespace RHI {

class SyncManager {
public:
    static constexpr uint32_t kMaxFramesInFlight = 3;

    SyncManager() noexcept = default;
    ~SyncManager() noexcept { Destroy(); }

    SyncManager(const SyncManager&) = delete;
    SyncManager& operator=(const SyncManager&) = delete;

    /// 初始化所有同步原语
    bool Initialize(VkDevice device) {
        m_Device = device;

        VkFenceCreateInfo fenceCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // 初始为 signaled

        VkSemaphoreCreateInfo semCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            VK_CHECK(vkCreateFence(device, &fenceCI, nullptr, &m_Fences[i]));
            VK_CHECK(vkCreateSemaphore(device, &semCI, nullptr, &m_ImageAvailable[i]));
            VK_CHECK(vkCreateSemaphore(device, &semCI, nullptr, &m_RenderFinished[i]));
        }

        return true;
    }

    /// 销毁所有同步原语
    void Destroy() {
        if (m_Device == VK_NULL_HANDLE) return;
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            if (m_Fences[i] != VK_NULL_HANDLE)
                vkDestroyFence(m_Device, m_Fences[i], nullptr);
            if (m_ImageAvailable[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Device, m_ImageAvailable[i], nullptr);
            if (m_RenderFinished[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(m_Device, m_RenderFinished[i], nullptr);
        }
    }

    /// 开始新帧：等待当前帧的 Fence（CPU 阻塞直到 GPU完成前一帧）
    void WaitForFrame() {
        if (m_Device == VK_NULL_HANDLE) return;

        // 等待当前帧可用
        VkResult res = vkWaitForFences(m_Device, 1, &m_Fences[m_CurrentFrame],
                                        VK_TRUE, UINT64_MAX);
        if (res != VK_SUCCESS) {
            std::fprintf(stderr, "[SyncManager] WaitForFences timeout!\n");
        }

        // 重置 Fence 供下一轮使用
        vkResetFences(m_Device, 1, &m_Fences[m_CurrentFrame]);
    }

    /// 获取当前帧的提交参数（用于 vkQueueSubmit）
    VkFence      CurrentFence() const noexcept { return m_Fences[m_CurrentFrame]; }
    VkSemaphore  CurrentImageAvailable() const noexcept { return m_ImageAvailable[m_CurrentFrame]; }
    VkSemaphore  CurrentRenderFinished() const noexcept { return m_RenderFinished[m_CurrentFrame]; }

    /// 推进到下一帧
    void AdvanceFrame() noexcept {
        m_CurrentFrame = (m_CurrentFrame + 1) % kMaxFramesInFlight;
    }

    /// 获取当前帧索引
    uint32_t GetCurrentFrame() const noexcept { return m_CurrentFrame; }

    /// 获取指定偏移的已完成帧索引（用于延迟销毁队列）
    uint32_t GetCompletedFrame(uint32_t offset = 1) const noexcept {
        return (m_CurrentFrame + kMaxFramesInFlight - offset) % kMaxFramesInFlight;
    }

private:
    VkDevice    m_Device{VK_NULL_HANDLE};
    uint32_t    m_CurrentFrame{0};

    VkFence     m_Fences[kMaxFramesInFlight]{VK_NULL_HANDLE};
    VkSemaphore m_ImageAvailable[kMaxFramesInFlight]{VK_NULL_HANDLE};
    VkSemaphore m_RenderFinished[kMaxFramesInFlight]{VK_NULL_HANDLE};
};

} // namespace RHI
} // namespace Engine