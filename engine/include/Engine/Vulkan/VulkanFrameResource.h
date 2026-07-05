#pragma once

/**
 * @file VulkanFrameResource.h
 * @brief Vulkan 帧资源 — 每帧的 Fence/Semaphore/CommandPool/Buffer
 *
 * 每帧拥有独立的资源，用于实现 FrameInFlight 多帧并行。
 * kMaxFramesInFlight = 3 帧同时在 GPU 管线中。
 */

#include "Engine/Types.h"
#include <vulkan/vulkan.h>

namespace Engine {

struct VulkanFrameResource {
    // ── 同步原语 ──
    VkFence         fence{VK_NULL_HANDLE};             // GPU → CPU 同步
    VkSemaphore     imageAvailable{VK_NULL_HANDLE};    // 交换链图像就绪
    VkSemaphore     renderFinished{VK_NULL_HANDLE};    // 渲染完成

    // ── 命令 ──
    VkCommandPool   commandPool{VK_NULL_HANDLE};        // 主线程命令池
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};      // 主线程命令缓冲

    // ── 动态 UBO ──
    VkBuffer        dynamicUBO{VK_NULL_HANDLE};         // 动态 UBO 缓冲
    VmaAllocation   dynamicUBOAlloc{VK_NULL_HANDLE};
    void*           dynamicUBOMapped{nullptr};          // 永久映射指针
    uint32_t        dynamicUBOOffset{0};                // 当前帧写入偏移
};

struct VulkanFrameContext {
    static constexpr uint32_t kMaxFramesInFlight = 3;
    VulkanFrameResource frames[kMaxFramesInFlight];
    uint32_t currentFrame{0};   // 当前帧索引
};

} // namespace Engine