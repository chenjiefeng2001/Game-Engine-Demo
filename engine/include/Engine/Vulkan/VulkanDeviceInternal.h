#pragma once

/**
 * @file VulkanDeviceInternal.h
 * @brief VulkanDevice 内部实现详细定义
 *
 * 整合：
 *   - SyncManager — 三重缓冲 Fence/Semaphore
 *   - DeferredDeletionQueue — 延迟资源销毁（3帧安全窗口）
 *   - StateTracker — 自动 Barrier 推导
 *   - FrameContext — 每帧资源三倍缓冲
 */

#include "Engine/Vulkan/VulkanCommon.h"
#include "Engine/Vulkan/VulkanFrameResource.h"
#include "Engine/Vulkan/DeferredDeletionQueue.h"
#include "Engine/Vulkan/StateTracker.h"
#include "Engine/Vulkan/SyncManager.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"

#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>

namespace Engine {
namespace RHI {

/**
 * @brief VulkanDevice 内部实现
 *
 * 生命周期：
 *   Initialize() → 每帧循环 → Shutdown()
 *
 * 每帧循环（SyncManager 驱动）：
 *   1. SyncManager::WaitForFrame() — CPU 等待 GPU
 *   2. DeletionQueue::Tick() — 释放已完成的资源
 *   3. AcquireNextImage — 获取交换链图像
 *   4. 录制命令
 *   5. QueueSubmit(SyncManager::CurrentFence)
 *   6. SyncManager::AdvanceFrame()
 */
struct VulkanDeviceImpl {
    // ── 核心 Vulkan 对象 ──
    VkInstance           instance{VK_NULL_HANDLE};
    VkPhysicalDevice     physicalDevice{VK_NULL_HANDLE};
    VkDevice             device{VK_NULL_HANDLE};
    VmaAllocator         vmaAllocator{VK_NULL_HANDLE};

    // ── 队列 ──
    VkQueue              graphicsQueue{VK_NULL_HANDLE};
    VkQueue              computeQueue{VK_NULL_HANDLE};
    VkQueue              transferQueue{VK_NULL_HANDLE};
    uint32_t             graphicsQueueIndex{UINT32_MAX};
    uint32_t             computeQueueIndex{UINT32_MAX};
    uint32_t             transferQueueIndex{UINT32_MAX};

    // ── 同步管理（三重缓冲） ──
    SyncManager          syncManager;

    // ── 延迟销毁队列 ──
    DeferredDeletionQueue deletionQueue;

    // ── 纹理状态追踪器 ──
    StateTracker         stateTracker;

    // ── 帧资源 ──
    VulkanFrameContext   frameContext;

    // ── 交换链 ──
    VkSwapchainKHR       swapChain{VK_NULL_HANDLE};
    VkSurfaceKHR         surface{VK_NULL_HANDLE};
    uint32_t             swapChainWidth{0};
    uint32_t             swapChainHeight{0};
    VkFormat             swapChainFormat{VK_FORMAT_B8G8R8A8_UNORM};
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;

    // ── 调试 ──
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
    bool                 enableValidation{false};

    // ── 线程命令池 ──
    std::unordered_map<std::thread::id, VkCommandPool> threadCmdPools;
    std::mutex threadPoolMutex;

    // ── 队列包装器 ──
    class VulkanQueue*   graphicsQueueWrapper{nullptr};

    // ── 状态 ──
    bool                 initialized{false};
    std::string          deviceName;
    void*                windowHandle{nullptr};
};

} // namespace RHI
} // namespace Engine