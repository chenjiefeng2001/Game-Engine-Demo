#pragma once

/**
 * @file VulkanFrameResource.h
 * @brief 帧资源管理 — 每帧独享资源 + 帧飞行同步
 *
 * 设计要点：
 *   - kMaxFramesInFlight = 3 三倍缓冲
 *   - 每帧独立 Fence/Semaphore/CommandPool/DescriptorPool/DynamicUBO
 *   - CPU 等待 GPU 完成后再重用该帧的资源
 *   - 环形索引：currentFrameIndex = (currentFrameIndex + 1) % kMaxFramesInFlight
 */

#include "Engine/Vulkan/VulkanCommon.h"

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// 每帧资源结构
// ════════════════════════════════════════════════════════════

struct VulkanFrameResource {
    // ── 同步原语 ──
    VkFence     fence{VK_NULL_HANDLE};           ///< CPU 等待 GPU（该帧完成）
    VkSemaphore imageAvailable{VK_NULL_HANDLE};   ///< 呈现信号（交换链图像可用）
    VkSemaphore renderFinished{VK_NULL_HANDLE};   ///< 渲染完成信号（用于呈现）

    // ── 主线程命令资源 ──
    VkCommandPool   commandPool{VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};

    // ── 动态 Uniform Buffer ──
    VkBuffer       dynamicUBO{VK_NULL_HANDLE};
    VmaAllocation  dynamicUBOAlloc{VK_NULL_HANDLE};
    void*          dynamicUBOMapped{nullptr};
    uint32_t       dynamicUBOOffset{0};           ///< 当前写入偏移（字节）

    // ── 描述符 ──
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};

    // ── 交换链图像索引 ──
    uint32_t imageIndex{0};

    /// 重置动态 UBO 偏移
    void ResetDynamicUBO() noexcept { dynamicUBOOffset = 0; }

    /// 分配动态 UBO 空间（返回对齐后的偏移）
    uint32_t AllocateDynamicUBOSpace(uint32_t size, uint32_t alignment = 256) noexcept {
        uint32_t aligned = (dynamicUBOOffset + alignment - 1) & ~(alignment - 1);
        dynamicUBOOffset = aligned + size;
        return aligned;
    }
};

// ════════════════════════════════════════════════════════════
// 帧上下文 — 管理 kMaxFramesInFlight 套资源
// ════════════════════════════════════════════════════════════

struct VulkanFrameContext {
    VulkanFrameResource frames[kMaxFramesInFlight];
    uint32_t currentFrameIndex{0};

    /// 获取当前帧资源引用
    VulkanFrameResource& Current() noexcept {
        return frames[currentFrameIndex];
    }

    const VulkanFrameResource& Current() const noexcept {
        return frames[currentFrameIndex];
    }

    /// 推进到下一帧（环形缓冲）
    void Advance() noexcept {
        currentFrameIndex = (currentFrameIndex + 1) % kMaxFramesInFlight;
    }

    /// 获取飞行帧索引（从 currentFrameIndex 往前数 n 帧）
    uint32_t GetFrameIndex(uint32_t offset) const noexcept {
        return (currentFrameIndex + kMaxFramesInFlight - offset) % kMaxFramesInFlight;
    }
};

} // namespace RHI
} // namespace Engine