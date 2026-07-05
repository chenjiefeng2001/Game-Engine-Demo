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
#include "vk_mem_alloc.h"

namespace Engine {
namespace RHI {

struct VulkanFrameResource {
    // ── 同步原语 ──
    VkFence         fence{VK_NULL_HANDLE};
    VkSemaphore     imageAvailable{VK_NULL_HANDLE};
    VkSemaphore     renderFinished{VK_NULL_HANDLE};

    // ── 命令 ──
    VkCommandPool   commandPool{VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};

    // ── 动态 UBO ──
    VkBuffer        dynamicUBO{VK_NULL_HANDLE};
    VmaAllocation   dynamicUBOAlloc{VK_NULL_HANDLE};
    void*           dynamicUBOMapped{nullptr};
    uint32_t        dynamicUBOOffset{0};
};

struct VulkanFrameContext {
    static constexpr uint32_t kMaxFramesInFlight = 3;
    VulkanFrameResource frames[kMaxFramesInFlight];
    uint32_t currentFrame{0};
};

} // namespace RHI
} // namespace Engine