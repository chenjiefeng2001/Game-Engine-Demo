#pragma once

/**
 * @file VulkanDeferredDeletion.h
 * @brief Vulkan 延迟销毁队列 — GPU 资源安全释放
 *
 * 设计原理：
 *   GPU 是异步执行的。当 CPU 请求销毁一个资源（VkBuffer、VkImage 等）时，
 *   GPU 可能还在使用它。立即销毁会导致 Use-After-Free 崩溃。
 *
 *   延迟销毁队列将资源推迟 3 帧（kMaxFramesInFlight）后再销毁，
 *   确保 GPU 已经完成了对该资源的所有访问。
 */

#include <vulkan/vulkan.h>
#include <vector>
#include <functional>
#include <cstdint>

namespace Engine {
namespace RHI {

class VulkanDeferredDeletion {
public:
    static constexpr uint32_t kMaxFrames = 3; // 与 kMaxFramesInFlight 匹配

    VulkanDeferredDeletion(VkDevice device) : m_Device(device) {}

    /** 每帧结束时调用，推进延迟队列 */
    void NextFrame() {
        m_CurrentFrame = (m_CurrentFrame + 1) % kMaxFrames;
        // 清理 3 帧前加入的待删资源
        auto& toDelete = m_PendingDeletions[m_CurrentFrame];
        for (auto& fn : toDelete) {
            fn();
        }
        toDelete.clear();
    }

    /** 延迟销毁 VkPipeline */
    void DestroyPipeline(VkPipeline pipeline) {
        if (pipeline == VK_NULL_HANDLE) return;
        m_PendingDeletions[m_CurrentFrame].push_back(
            [device = m_Device, pipeline]() {
                vkDestroyPipeline(device, pipeline, nullptr);
            });
    }

    /** 延迟销毁 VkImageView */
    void DestroyImageView(VkImageView view) {
        if (view == VK_NULL_HANDLE) return;
        m_PendingDeletions[m_CurrentFrame].push_back(
            [device = m_Device, view]() {
                vkDestroyImageView(device, view, nullptr);
            });
    }

    /** 延迟销毁 VkBuffer */
    void DestroyBuffer(VkBuffer buffer) {
        if (buffer == VK_NULL_HANDLE) return;
        m_PendingDeletions[m_CurrentFrame].push_back(
            [device = m_Device, buffer]() {
                vkDestroyBuffer(device, buffer, nullptr);
            });
    }

    /** 延迟销毁 VkImage */
    void DestroyImage(VkImage image) {
        if (image == VK_NULL_HANDLE) return;
        m_PendingDeletions[m_CurrentFrame].push_back(
            [device = m_Device, image]() {
                vkDestroyImage(device, image, nullptr);
            });
    }

    /** 延迟销毁 VkFramebuffer */
    void DestroyFramebuffer(VkFramebuffer fb) {
        if (fb == VK_NULL_HANDLE) return;
        m_PendingDeletions[m_CurrentFrame].push_back(
            [device = m_Device, fb]() {
                vkDestroyFramebuffer(device, fb, nullptr);
            });
    }

    /** 强制立即清理所有待删资源（Shutdown 时调用） */
    void FlushAll() {
        for (auto& frame : m_PendingDeletions) {
            for (auto& fn : frame) fn();
            frame.clear();
        }
    }

    /** 添加任意自定义延迟回调 */
    void AddCallback(std::function<void()>&& fn) {
        m_PendingDeletions[m_CurrentFrame].push_back(std::move(fn));
    }

private:
    VkDevice m_Device;
    uint32_t m_CurrentFrame = 0;
    std::vector<std::function<void()>> m_PendingDeletions[kMaxFrames];
};

} // namespace RHI
} // namespace Engine