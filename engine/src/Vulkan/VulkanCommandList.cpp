/**
 * @file VulkanCommandList.cpp
 * @brief Vulkan 命令列表实现 — 命令录制接口
 *
 * 设计要点：
 *   - 每个 CommandList 从当前线程的 TLS 命令池分配
 *   - 使用 VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
 *   - 自动阶段屏障推导 (StateToStage)
 *   - 支持 Dynamic Offset UBO 绑定
 */

#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Vulkan/VulkanCommon.h"
#include "Engine/Vulkan/VulkanFrameResource.h"

#include <cstdio>
#include <cstring>

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// VulkanCommandList::Impl
// ════════════════════════════════════════════════════════════

struct VulkanCommandList::Impl {
    VkCommandBuffer     cmdBuffer{VK_NULL_HANDLE};
    VulkanDevice*       device{nullptr};
    CommandListType     type{CommandListType::Direct};
    bool                isRecording{false};

    // 当前状态缓存
    VkPipelineLayout    currentPipelineLayout{VK_NULL_HANDLE};
    VkPipeline          currentPipeline{VK_NULL_HANDLE};
    VkDescriptorSet     currentDescriptorSet{VK_NULL_HANDLE};
    uint32_t            currentDynamicOffset{0};

    // 屏障批处理
    static constexpr uint32_t kMaxBarriers = 16;
    VkImageMemoryBarrier  imageBarriers[kMaxBarriers];
    VkPipelineStageFlags  srcStageMask{0};
    VkPipelineStageFlags  dstStageMask{0};
    uint32_t              barrierCount{0};

    void FlushBarriers() {
        if (barrierCount == 0) return;

        vkCmdPipelineBarrier(cmdBuffer,
                             srcStageMask, dstStageMask,
                             0,
                             0, nullptr,
                             0, nullptr,
                             barrierCount, imageBarriers);

        barrierCount = 0;
        srcStageMask = 0;
        dstStageMask = 0;
    }
};

VulkanCommandList::VulkanCommandList()
    : m_Impl(std::make_unique<Impl>()) {}

VulkanCommandList::~VulkanCommandList() = default;

// ════════════════════════════════════════════════════════════
// 录制周期
// ════════════════════════════════════════════════════════════

void VulkanCommandList::Begin() {
    if (m_Impl->isRecording) return;

    // 从设备获取线程命令池
    // 注意：m_Impl->device 需要在创建时设置
    // 这里简化处理：使用 main 命令缓冲
    if (m_Impl->cmdBuffer == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[VulkanCommandList] No command buffer allocated\n");
        return;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(m_Impl->cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[VulkanCommandList] vkBeginCommandBuffer failed\n");
        return;
    }

    m_Impl->isRecording = true;
    m_Impl->barrierCount = 0;
}

void VulkanCommandList::End() {
    if (!m_Impl->isRecording) return;

    // 刷新未提交的屏障
    m_Impl->FlushBarriers();

    VkResult result = vkEndCommandBuffer(m_Impl->cmdBuffer);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[VulkanCommandList] vkEndCommandBuffer failed\n");
        return;
    }

    m_Impl->isRecording = false;
}

void VulkanCommandList::Reset() {
    if (m_Impl->cmdBuffer == VK_NULL_HANDLE) return;

    vkResetCommandBuffer(m_Impl->cmdBuffer, 0);
    m_Impl->isRecording = false;
    m_Impl->barrierCount = 0;
}

// ════════════════════════════════════════════════════════════
// 管线状态
// ════════════════════════════════════════════════════════════

void VulkanCommandList::SetPipelineState(IRHIPipelineState* pso) {
    auto* vkPso = static_cast<VulkanPipelineState*>(pso);
    VkPipeline pipeline = vkPso->GetVkPipeline();
    VkPipelineLayout layout = vkPso->GetVkPipelineLayout();
    
    if (pipeline != VK_NULL_HANDLE && m_Impl->cmdBuffer != VK_NULL_HANDLE) {
        vkCmdBindPipeline(m_Impl->cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }
    
    m_Impl->currentPipeline = pipeline;
    m_Impl->currentPipelineLayout = layout;
}

// ════════════════════════════════════════════════════════════
// 几何体绑定
// ════════════════════════════════════════════════════════════

void VulkanCommandList::SetVertexBuffer(uint32 slot, IRHIBuffer* buffer,
                                         uint32 stride, uint32 offset) {
    auto* vkBuffer = static_cast<VulkanBuffer*>(buffer);
    VkBuffer vkbuf = vkBuffer->GetVkBuffer();
    VkDeviceSize vkOffset = offset;

    vkCmdBindVertexBuffers(m_Impl->cmdBuffer, slot, 1, &vkbuf, &vkOffset);
}

void VulkanCommandList::SetIndexBuffer(IRHIBuffer* buffer, uint32 offset) {
    auto* vkBuffer = static_cast<VulkanBuffer*>(buffer);
    VkBuffer vkbuf = vkBuffer->GetVkBuffer();

    // 默认使用 VK_INDEX_TYPE_UINT32
    vkCmdBindIndexBuffer(m_Impl->cmdBuffer, vkbuf, offset, VK_INDEX_TYPE_UINT32);
}

void VulkanCommandList::SetPrimitiveTopology(PrimitiveTopology topology) {
    // Vulkan 拓扑在 PSO 创建时设置
    // 这里如果需要动态改变，需要使用 VK_EXT_extended_dynamic_state
    // 简化：忽略
    (void)topology;
}

// ════════════════════════════════════════════════════════════
// 绘制
// ════════════════════════════════════════════════════════════

void VulkanCommandList::DrawIndexed(uint32 indexCount, uint32 startIndex,
                                     uint32 baseVertex) {
    m_Impl->FlushBarriers();
    vkCmdDrawIndexed(m_Impl->cmdBuffer, indexCount, 1, startIndex, baseVertex, 0);
}

void VulkanCommandList::Draw(uint32 vertexCount, uint32 startVertex) {
    m_Impl->FlushBarriers();
    vkCmdDraw(m_Impl->cmdBuffer, vertexCount, 1, startVertex, 0);
}

void VulkanCommandList::DrawIndexedIndirect(IRHIBuffer* argsBuffer, uint32 offset) {
    m_Impl->FlushBarriers();
    auto* vkBuffer = static_cast<VulkanBuffer*>(argsBuffer);
    vkCmdDrawIndexedIndirect(m_Impl->cmdBuffer, vkBuffer->GetVkBuffer(),
                              offset, 1, sizeof(VkDrawIndexedIndirectCommand));
}

// ════════════════════════════════════════════════════════════
// 视口 / 裁剪
// ════════════════════════════════════════════════════════════

void VulkanCommandList::SetViewport(const Viewport& vp) {
    VkViewport vkViewport{};
    vkViewport.x = vp.x;
    vkViewport.y = vp.y;
    vkViewport.width = vp.width;
    vkViewport.height = vp.height;
    vkViewport.minDepth = vp.minDepth;
    vkViewport.maxDepth = vp.maxDepth;

    vkCmdSetViewport(m_Impl->cmdBuffer, 0, 1, &vkViewport);
}

void VulkanCommandList::SetScissorRect(const Rect& rect) {
    VkRect2D vkRect{};
    vkRect.offset.x = rect.x;
    vkRect.offset.y = rect.y;
    vkRect.extent.width = static_cast<uint32_t>(rect.width);
    vkRect.extent.height = static_cast<uint32_t>(rect.height);

    vkCmdSetScissor(m_Impl->cmdBuffer, 0, 1, &vkRect);
}

// ════════════════════════════════════════════════════════════
// 资源屏障（自动阶段推导 + 批处理）
// ════════════════════════════════════════════════════════════

void VulkanCommandList::ResourceBarrier(uint32 count,
                                         const ResourceBarrierDesc* barriers) {
    for (uint32_t i = 0; i < count; ++i) {
        const auto& src = barriers[i];

        // 如果是 Buffer 屏障或不需要真正内存屏障的转换，跳过
        if (src.type != ResourceBarrierDesc::Type::Transition) {
            continue;
        }

        if (m_Impl->barrierCount >= Impl::kMaxBarriers) {
            m_Impl->FlushBarriers();
        }

        auto& vkBarrier = m_Impl->imageBarriers[m_Impl->barrierCount];
        VkPipelineStageFlags srcStage, dstStage;

        ConvertBarrierDesc(src, vkBarrier, srcStage, dstStage);

        m_Impl->srcStageMask |= srcStage;
        m_Impl->dstStageMask |= dstStage;
        m_Impl->barrierCount++;
    }

    // 如果达到批处理上限，立即刷新
    if (m_Impl->barrierCount >= Impl::kMaxBarriers) {
        m_Impl->FlushBarriers();
    }
}

// ════════════════════════════════════════════════════════════
// 描述符绑定（占位实现）
// ════════════════════════════════════════════════════════════

void VulkanCommandList::SetConstantBuffer(uint32 set, uint32 binding,
                                            IRHIBuffer* buffer,
                                            uint64_t offset, uint64_t size)
{
    // TODO: Vulkan 后端实现
    // 调用 vkCmdBindDescriptorSets 或 vkCmdPushDescriptorSetKHR
    (void)set;
    (void)binding;
    (void)buffer;
    (void)offset;
    (void)size;
}

void VulkanCommandList::SetShaderResource(uint32 set, uint32 binding,
                                            IRHITexture* texture)
{
    // TODO: Vulkan 后端实现
    (void)set;
    (void)binding;
    (void)texture;
}

// ════════════════════════════════════════════════════════════
// 查询
// ════════════════════════════════════════════════════════════

CommandListType VulkanCommandList::GetType() const noexcept {
    return m_Impl->type;
}

void VulkanCommandList::SetVkCommandBuffer(VkCommandBuffer cmdBuf) noexcept {
    m_Impl->cmdBuffer = cmdBuf;
}

void VulkanCommandList::SetVkPipelineState(VkPipeline pipeline, VkPipelineLayout layout) noexcept {
    m_Impl->currentPipeline = pipeline;
    m_Impl->currentPipelineLayout = layout;
}

void VulkanCommandList::SetDevice(VulkanDevice* device) noexcept {
    m_Impl->device = device;
}

VkCommandBuffer VulkanCommandList::GetVkCommandBuffer() const noexcept {
    return m_Impl->cmdBuffer;
}

} // namespace RHI
} // namespace Engine