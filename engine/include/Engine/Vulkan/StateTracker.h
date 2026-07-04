#pragma once

/**
 * @file StateTracker.h
 * @brief 资源状态追踪器 — 自动推导 VkPipelineBarrier
 *
 * 核心理念：
 *   上层（RHI 抽象层）只关心 ResourceState（ShaderResource/RenderTarget/Present），
 *   底层 StateTracker 自动对比"当前状态"与"目标状态"，
 *   在需要时自动插入 VkImageMemoryBarrier。
 *
 * 这解决了 Vulkan 开发中最大的痛点：手动管理 VkImageLayout 转换。
 *
 * 设计要点：
 *   - 每个 IRHITexture 关联一个当前状态
 *   - 当 CommandList 需要使用纹理时，检查状态是否匹配
 *   - 不匹配则自动插入 Barrier
 *   - 支持批量合并 Barrier（最多 16 个一次提交）
 */

#include "Engine/Vulkan/VulkanCommon.h"
#include <unordered_map>

namespace Engine {
namespace RHI {

// 前向声明
class VulkanTexture;

/**
 * @brief 每纹理状态追踪器
 */
class StateTracker {
public:
    StateTracker() noexcept = default;

    StateTracker(const StateTracker&) = delete;
    StateTracker& operator=(const StateTracker&) = delete;

    // ════════════════════════════════════════════════════════
    // 状态查询与更新
    // ════════════════════════════════════════════════════════

    /// 获取纹理的当前状态（默认 Undefined）
    ResourceState GetState(VulkanTexture* texture) const noexcept;

    /// 设置纹理的当前状态（用于初始化或外部强制同步）
    void SetState(VulkanTexture* texture, ResourceState state) noexcept;

    /// 检查是否需要 Barrier
    bool NeedsBarrier(VulkanTexture* texture, ResourceState targetState) const noexcept;

    // ════════════════════════════════════════════════════════
    // Barrier 收集与提交
    // ════════════════════════════════════════════════════════

    /**
     * @brief 记录一个需要 Barrier 的纹理转换
     *
     * 不立即提交，而是批处理缓存中。
     * 调用 Flush() 时统一提交到 VkCommandBuffer。
     *
     * @param texture  目标纹理
     * @param newState 目标状态
     * @return true  已记录到批处理缓存中
     * @return false 无需 Barrier（状态已匹配）
     */
    bool RecordTransition(VulkanTexture* texture, ResourceState newState);

    /**
     * @brief 将所有已记录的 Barrier 提交到 CommandBuffer
     *
     * @param cmdBuffer 目标 VkCommandBuffer
     */
    void Flush(VkCommandBuffer cmdBuffer);

    /**
     * @brief 清空 Barrier 缓存（不提交）
     */
    void Reset() noexcept;

private:
    // ════════════════════════════════════════════════════════
    // 内部状态
    // ════════════════════════════════════════════════════════

    /// 每个纹理的当前布局状态
    std::unordered_map<VulkanTexture*, ResourceState> m_TextureStates;

    // ── Barrier 批处理缓冲区 ──
    static constexpr uint32_t kMaxBarriers = 16;

    VkImageMemoryBarrier  m_PendingBarriers[kMaxBarriers];
    VkPipelineStageFlags  m_SrcStageMask{0};
    VkPipelineStageFlags  m_DstStageMask{0};
    uint32_t              m_PendingCount{0};
};

} // namespace RHI
} // namespace Engine