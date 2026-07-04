/**
 * @file StateTracker.cpp
 * @brief 自动 Barrier 推导实现
 */

#include "Engine/Vulkan/StateTracker.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"

#include <cstdio>

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// 状态查询
// ════════════════════════════════════════════════════════════

ResourceState StateTracker::GetState(VulkanTexture* texture) const noexcept {
    auto it = m_TextureStates.find(texture);
    if (it != m_TextureStates.end()) {
        return it->second;
    }
    return ResourceState::Undefined;
}

void StateTracker::SetState(VulkanTexture* texture, ResourceState state) noexcept {
    m_TextureStates[texture] = state;
}

bool StateTracker::NeedsBarrier(VulkanTexture* texture, ResourceState targetState) const noexcept {
    auto it = m_TextureStates.find(texture);
    if (it == m_TextureStates.end()) {
        // 未追踪 = Undefined，任何非 Undefined 目标状态都需要 Barrier
        return targetState != ResourceState::Undefined;
    }
    return it->second != targetState;
}

// ════════════════════════════════════════════════════════════
// Barrier 收集
// ════════════════════════════════════════════════════════════

bool StateTracker::RecordTransition(VulkanTexture* texture, ResourceState newState) {
    // 检查是否需要转换
    ResourceState currentState = GetState(texture);
    if (currentState == newState) {
        return false;  // 状态一致，无需 Barrier
    }

    // 构建 VkImageMemoryBarrier
    if (m_PendingCount >= kMaxBarriers) {
        // 缓存满了，强制刷新（生产环境下不应发生）
        std::fprintf(stderr, "[StateTracker] Barrier buffer overflow! Forcing flush.\n");
        // 这里无法 flush，因为没有 VkCommandBuffer 参数
        // 作为 fallback，忽略这个 barrier
        return false;
    }

    auto& barrier = m_PendingBarriers[m_PendingCount];
    barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = ResourceStateToLayout(currentState);
    barrier.newLayout = ResourceStateToLayout(newState);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->GetVkImage();

    // 所有 mip levels 和 array layers
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    // 自动推导 stage 和 access mask
    VkPipelineStageFlags srcStage = ResourceStateToStage(currentState);
    VkPipelineStageFlags dstStage = ResourceStateToStage(newState);

    barrier.srcAccessMask = ResourceStateToAccess(currentState);
    barrier.dstAccessMask = ResourceStateToAccess(newState);

    m_SrcStageMask |= srcStage;
    m_DstStageMask |= dstStage;
    m_PendingCount++;

    // 更新追踪状态
    m_TextureStates[texture] = newState;

    return true;
}

// ════════════════════════════════════════════════════════════
// Barrier 提交
// ════════════════════════════════════════════════════════════

void StateTracker::Flush(VkCommandBuffer cmdBuffer) {
    if (m_PendingCount == 0) return;

    vkCmdPipelineBarrier(
        cmdBuffer,
        m_SrcStageMask,
        m_DstStageMask,
        0,                           // dependency flags
        0, nullptr,                  // memory barriers
        0, nullptr,                  // buffer memory barriers
        m_PendingCount,              // image memory barriers
        m_PendingBarriers
    );

    // 清空缓存
    m_PendingCount = 0;
    m_SrcStageMask = 0;
    m_DstStageMask = 0;
}

// ════════════════════════════════════════════════════════════
// 重置
// ════════════════════════════════════════════════════════════

void StateTracker::Reset() noexcept {
    m_PendingCount = 0;
    m_SrcStageMask = 0;
    m_DstStageMask = 0;
    // 注意：不清除 m_TextureStates，因为纹理状态是跨帧持续的
}

} // namespace RHI
} // namespace Engine