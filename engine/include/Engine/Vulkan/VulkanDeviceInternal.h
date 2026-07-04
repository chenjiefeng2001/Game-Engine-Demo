#pragma once

/**
 * @file VulkanDeviceInternal.h
 * @brief VulkanDevice Pimpl 内部结构的完善定义
 *
 * 此文件将 VulkanDevice::Impl 的完整定义从 .cpp 中提取出来，
 * 使 VulkanCommandList 等类可以直接访问 Impl 的成员。
 *
 * 包含：
 *   - DeferredDeletionQueue — 延迟资源销毁
 *   - StateTracker — 自动 Barrier 推导
 *   - FrameContext — 三倍缓冲同步
 *   - 所有 Vulkan 核心对象
 */

#include "Engine/Vulkan/VulkanCommon.h"
#include "Engine/Vulkan/VulkanFrameResource.h"
#include "Engine/Vulkan/DeferredDeletionQueue.h"
#include "Engine/Vulkan/StateTracker.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"

#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>

namespace Engine {
namespace RHI {

/**
 * @brief VulkanDevice 内部实现详细定义
 */
struct VulkanDeviceImpl {
    // ── 核心 Vulkan 对象 ──
    VkInstance           instance{VK_NULL_HANDLE};
    VkPhysicalDevice     physicalDevice{VK_NULL_HANDLE};
    VkDevice             device{VK_NULL_HANDLE};

    // ── 队列 ──
    VkQueue              graphicsQueue{VK_NULL_HANDLE};
    VkQueue              computeQueue{VK_NULL_HANDLE};
    VkQueue              transferQueue{VK_NULL_HANDLE};
    uint32_t             graphicsQueueIndex{UINT32_MAX};
    uint32_t             computeQueueIndex{UINT32_MAX};
    uint32_t             transferQueueIndex{UINT32_MAX};

    // ── VMA ──
    VmaAllocator         vmaAllocator{VK_NULL_HANDLE};

    // ── 交换链 ──
    VkSwapchainKHR       swapChain{VK_NULL_HANDLE};
    VkSurfaceKHR         surface{VK_NULL_HANDLE};
    uint32_t             swapChainWidth{0};
    uint32_t             swapChainHeight{0};
    VkFormat             swapChainFormat{VK_FORMAT_B8G8R8A8_UNORM};
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;

    // ── 帧资源（三倍缓冲） ──
    VulkanFrameContext   frameContext;

    // ── 延迟销毁队列 ──
    DeferredDeletionQueue deletionQueue;

    // ── 纹理状态追踪器 ──
    StateTracker         stateTracker;

    // ── 调试 ──
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
    bool                 enableValidation{false};

    // ── 线程命令池 ──
    std::unordered_map<std::thread::id, VkCommandPool> threadCmdPools;
    std::mutex threadPoolMutex;

    // ── 队列包装器 ──
    VulkanQueue*         graphicsQueueWrapper{nullptr};

    // ── 状态 ──
    bool                 initialized{false};
    std::string          deviceName;
    void*                windowHandle{nullptr};
};

} // namespace RHI
} // namespace Engine