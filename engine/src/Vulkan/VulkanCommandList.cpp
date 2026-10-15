/**
 * @file VulkanCommandList.cpp
 * @brief Vulkan 命令列表实现 — 命令录制接口
 *
 * 设计要点：
 *   - 每个 CommandList 从当前线程的 TLS 命令池分配
 *   - 使用 VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
 *   - 自动阶段屏障推导 (StateToStage)
 *   - 支持 Dynamic Offset UBO 绑定
 *
 * 安全策略：
 *   所有 VkCmd* 调用前必须检查 isRecording && cmdBuffer != VK_NULL_HANDLE，
 *   防止在未录制状态或无效句柄下触发 Vulkan 验证层 abort()。
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
      bool                renderingActive{false};
      // 当前渲染作用域的附件。EndRendering 需要它们才能把交换链图像转成
      // PRESENT_SRC_KHR：RHI 接口不区分渲染目标与呈现目标，只有 Vulkan 侧
      // 知道某个 VulkanTexture 是否是 swapchain backbuffer。
      IRHITexture*        renderColors[RenderTargetDesc::kMaxColorAttachments]{};
      IRHITexture*        renderDepth{nullptr};
      // 当前绑定的 PSO：SetConstantBuffer 需要它的 descriptor set 布局。
      VulkanPipelineState* currentPso{nullptr};
      // 每个 set 索引只分配一次描述符集并绑定一次，后续写入累积到同一个集合。
      // 此前每次 Set*Resource 都新分配一个 set 并绑定：同一 shader 同时用 UBO 与
      // 纹理时，第二次调用会覆盖第一次的绑定，UBO 就丢了。
      VkDescriptorSet descriptorSets[4]{};
      VkDescriptorSetLayout boundSetLayouts[4]{};
      void ResetDescriptorSets() {
          for (auto& s : descriptorSets) s = VK_NULL_HANDLE;
          for (auto& l : boundSetLayouts) l = VK_NULL_HANDLE;
      }
      // 取（必要时分配并绑定）某个 set 索引的描述符集。
      VkDescriptorSet AcquireDescriptorSet(uint32 index) {
          if (index >= 4 || !currentPso || !device) return VK_NULL_HANDLE;
          if (descriptorSets[index] != VK_NULL_HANDLE &&
              boundSetLayouts[index] == currentPso->GetDescriptorSetLayout())
              return descriptorSets[index];
          ResetDescriptorSets();
          VkDescriptorSetLayout lay = currentPso->GetDescriptorSetLayout();
          VkDescriptorSet s = device->AllocateDescriptorSet(lay);
          if (s == VK_NULL_HANDLE) return VK_NULL_HANDLE;
          VkPipelineBindPoint bp = currentPso->IsCompute() ? VK_PIPELINE_BIND_POINT_COMPUTE
                                                            : VK_PIPELINE_BIND_POINT_GRAPHICS;
          vkCmdBindDescriptorSets(cmdBuffer, bp, currentPso->GetVkPipelineLayout(),
                                  index, 1, &s, 0, nullptr);
          descriptorSets[index] = s;
          boundSetLayouts[index] = lay;
          return s;
      }
      bool IsReady() const noexcept { return isRecording && cmdBuffer != VK_NULL_HANDLE; }

    void FlushBarriers() {
        if (barrierCount == 0) return;
        if (cmdBuffer == VK_NULL_HANDLE) { barrierCount = 0; return; }

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

    // 命令缓冲：首次 Begin 时从设备池惰性分配。此前这里只在 cmdBuffer
    // 已存在时继续，而**没有任何地方分配它**，于是 Begin 永远失败。
    if (m_Impl->cmdBuffer == VK_NULL_HANDLE) {
        if (!m_Impl->device) {
            std::fprintf(stderr, "[VulkanCommandList] No device set on command list\n");
            return;
        }
        VkCommandPool pool = m_Impl->device->GetOrCreateThreadCommandPool();
        if (pool == VK_NULL_HANDLE) {
            std::fprintf(stderr, "[VulkanCommandList] No command pool available\n");
            return;
        }
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(m_Impl->device->GetVkDevice(), &alloc, &m_Impl->cmdBuffer) != VK_SUCCESS) {
            std::fprintf(stderr, "[VulkanCommandList] vkAllocateCommandBuffers failed\n");
            m_Impl->cmdBuffer = VK_NULL_HANDLE;
            return;
        }
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(m_Impl->cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[VulkanCommandList] vkBeginCommandBuffer failed (%d)\n", result);
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
        std::fprintf(stderr, "[VulkanCommandList] vkEndCommandBuffer failed (%d)\n", result);
        return;
    }

    m_Impl->isRecording = false;
}

void VulkanCommandList::Submit() {
      // 提交语义此前藏在 VulkanSwapChain::Present() 里：它顺手把调用方录好的命令
      // 缓冲提交了。于是离屏 / 无窗口场景根本没有提交入口。现在显式提供。
if (!m_Impl->device) return;
      if (m_Impl->cmdBuffer == VK_NULL_HANDLE) return;
      // 还没 End()：命令缓冲仍是录制态，提交非法。
      if (m_Impl->isRecording) {
          std::fprintf(stderr, "[VulkanCommandList] Submit() while still recording; call End() first\n");
          return;
      }
      VulkanFrameContext& frameCtx = m_Impl->device->GetFrameContext();
      VulkanFrameResource& fr = frameCtx.frames[frameCtx.currentFrame];
      VkDevice dev = m_Impl->device->GetVkDevice();

      VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
      si.commandBufferCount = 1;
      si.pCommandBuffers = &m_Impl->cmdBuffer;
      // 有交换链时必须先等 acquire，否则任何触碰 backbuffer 的命令都是竞态。
      //
      // 刻意**不**在这里 vkWaitForFences：那属于「复用帧槽位」的等待，已由
      // AcquireNextImage() 完成（它等完并重置了 fence）。重复等待会挂死 ——
      // 此时 fence 尚未被本次提交 signal，等待一个永不signal 的 fence 是死锁。
      if (fr.imageAvailable != VK_NULL_HANDLE) {
          si.waitSemaphoreCount = 1;
          si.pWaitSemaphores = &fr.imageAvailable;
          si.pWaitDstStageMask = &waitStages;
      }
      // signal renderFinished：Present() 要靠它等待渲染完成。
      if (fr.renderFinished != VK_NULL_HANDLE) {
          si.signalSemaphoreCount = 1;
          si.pSignalSemaphores = &fr.renderFinished;
      }
      if (vkQueueSubmit(m_Impl->device->GetGraphicsQueue(), 1, &si,
                        fr.fence != VK_NULL_HANDLE ? fr.fence : VK_NULL_HANDLE) != VK_SUCCESS) {
          std::fprintf(stderr, "[VulkanCommandList] vkQueueSubmit failed\n");
      }
  }

void VulkanCommandList::Reset() {
      if (m_Impl->cmdBuffer == VK_NULL_HANDLE) return;
      vkResetCommandBuffer(m_Impl->cmdBuffer, 0);
    m_Impl->isRecording = false;
    m_Impl->barrierCount = 0;
    m_Impl->renderingActive = false;
}

// ════════════════════════════════════════════════════════════
// 渲染作用域（Vulkan dynamic rendering）
// ════════════════════════════════════════════════════════════

void VulkanCommandList::BeginRendering(const RenderTargetDesc& desc) {
    if (!m_Impl->IsReady()) return;
    if (m_Impl->renderingActive) {
        std::fprintf(stderr, "[Vulkan] BeginRendering called while already in a scope\n");
        return;
    }
    if (desc.colorCount == 0 || desc.colorCount > RenderTargetDesc::kMaxColorAttachments) {
        std::fprintf(stderr, "[Vulkan] BeginRendering: colorCount=%u out of range\n", desc.colorCount);
        return;
    }

    VkRenderingAttachmentInfo attachments[RenderTargetDesc::kMaxColorAttachments]{};
    uint32 usable = 0;
    uint32 extentW = 0, extentH = 0;
    for (uint32 i = 0; i < desc.colorCount; ++i) {
        auto* tex = static_cast<VulkanTexture*>(desc.color[i]);
        VkImageView view = tex->GetVkImageView();
        if (view == VK_NULL_HANDLE) {
            std::fprintf(stderr, "[Vulkan] BeginRendering: color[%u] has no image view\n", i);
            continue;
        }
VkRenderingAttachmentInfo& a = attachments[usable++];
          a.sType     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
          a.imageView = view;
          // 动态渲染（vkCmdBeginRendering）只接受 COLOR_ATTACHMENT_OPTIMAL /
          // GENERAL / SHARED_PRESENT_KHR。原先写的是 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL，
          // 那不是动态渲染的合法附件布局。
          a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp    = desc.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp   = VK_ATTACHMENT_STORE_OP_STORE;
        // clearValue 必须赋值：loadOp=CLEAR 读的是它，漏掉会清成 (0,0,0,0)。
        // 路径为 VkClearValue::color → VkClearColorValue::float32：
        // 匿名成员 color[] 不支持下标，必须走具名成员。
for (uint32 ch = 0; ch < 4; ++ch) a.clearValue.color.float32[ch] = desc.clearColor[ch];
          m_Impl->renderColors[usable - 1] = desc.color[i];
          extentW = tex->GetWidth();
          extentH = tex->GetHeight();
      }
    if (usable == 0) return;

    VkRenderingAttachmentInfo depthAttachment{};
    bool hasDepth = false;
    if (desc.depth) {
        auto* dtex = static_cast<VulkanTexture*>(desc.depth);
        if (dtex->GetVkImageView() != VK_NULL_HANDLE) {
            depthAttachment.sType      = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView  = dtex->GetVkImageView();
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp     = desc.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAttachment.storeOp    = VK_ATTACHMENT_STORE_OP_STORE;
            dtex->SetLayout(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            hasDepth = true;
        }
    }

    VkRenderingInfo ri{};
    ri.sType            = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea       = { { 0, 0 }, { extentW, extentH } };
    ri.layerCount       = 1;
    ri.colorAttachmentCount = usable;
    ri.pColorAttachments    = attachments;
    if (hasDepth) {
        // VkRenderingInfo 没有 depthAttachmentCount 字段：是否附加深度由
        // pDepthAttachment 是否为空决定。
        ri.pDepthAttachment = &depthAttachment;
    }

    m_Impl->FlushBarriers();

    // 附件必须真的处于动态渲染声明的布局。此前这里只把 renderColors 的布局
    // 「记」成目标布局，却不发出任何转换：刚 acquire 回来的 swapchain 图像追踪
    // 布局是 UNDEFINED，于是 vkCmdBeginRendering 读的是 UNDEFINED、present 时
    // 也仍是 UNDEFINED。离屏路径因为调用方自己发过屏障所以没暴露。
    for (uint32 i = 0; i < usable; ++i) {
        auto* vkTex = static_cast<VulkanTexture*>(m_Impl->renderColors[i]);
        if (vkTex->GetLayout() == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) continue;
        VkImageMemoryBarrier b{};
        b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.image            = vkTex->GetVkImage();   // 必填，漏掉整条 barrier 会被丢弃
        b.oldLayout        = vkTex->GetLayout();
        b.newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask    = b.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
                                 ? 0 : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(m_Impl->cmdBuffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        vkTex->SetLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    if (hasDepth) {
        auto* dtex = static_cast<VulkanTexture*>(desc.depth);
        if (dtex->GetLayout() != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
            VkImageMemoryBarrier b{};
            b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.image            = dtex->GetVkImage();
            b.oldLayout        = dtex->GetLayout();
            b.newLayout        = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            b.srcAccessMask    = b.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
                                     ? 0 : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask    = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(m_Impl->cmdBuffer,
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
            dtex->SetLayout(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        }
        m_Impl->renderDepth = desc.depth;
    }

    vkCmdBeginRendering(m_Impl->cmdBuffer, &ri);
    m_Impl->renderingActive = true;

    // 视口与裁剪是动态状态：必须在这里设置，否则绘制被整体裁掉且不报任何错误。
    if (extentW > 0 && extentH > 0) {
        VkViewport vp{};
        vp.x = 0.0f; vp.y = 0.0f;
        vp.width  = static_cast<float>(extentW);
        vp.height = static_cast<float>(extentH);
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { extentW, extentH };
        vkCmdSetViewport(m_Impl->cmdBuffer, 0, 1, &vp);
        vkCmdSetScissor(m_Impl->cmdBuffer, 0, 1, &scissor);
    }
}

void VulkanCommandList::EndRendering() {
      if (!m_Impl->IsReady() || !m_Impl->renderingActive) return;
      vkCmdEndRendering(m_Impl->cmdBuffer);
      m_Impl->renderingActive = false;

      // 交换链图像必须在 present 之前转到 PRESENT_SRC_KHR。EndRendering 之前
      // 完全没有这一步，于是每一帧的 vkQueuePresentKHR 都是非法调用
      // （VUID-VkPresentInfoKHR-pImageIndices-01430），由 Khronos validation
      // layer 在真实窗口路径上抓出。普通渲染目标保持 COLOR_ATTACHMENT_OPTIMAL。
      VkImageMemoryBarrier b{};
      b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      b.newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
      b.srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      b.dstAccessMask    = 0;
      uint32_t presented = 0;
      VkImageMemoryBarrier batch[RenderTargetDesc::kMaxColorAttachments];
      for (auto* c : m_Impl->renderColors) {
          if (!c) continue;
          auto* vkTex = static_cast<VulkanTexture*>(c);
          if (!vkTex->IsSwapChainImage()) continue;
          b.image = vkTex->GetVkImage();
          batch[presented++] = b;
          vkTex->SetLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
      }
      if (presented > 0) {
          vkCmdPipelineBarrier(m_Impl->cmdBuffer,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                               0, 0, nullptr, 0, nullptr, presented, batch);
      }
      for (auto& slot : m_Impl->renderColors) slot = nullptr;
      m_Impl->renderDepth = nullptr;
  }

// ════════════════════════════════════════════════════════════
// 管线状态
// ════════════════════════════════════════════════════════════

void VulkanCommandList::SetPipelineState(IRHIPipelineState* pso) {
    auto* vkPso = static_cast<VulkanPipelineState*>(pso);
    VkPipeline pipeline = vkPso->GetVkPipeline();
    VkPipelineLayout layout = vkPso->GetVkPipelineLayout();
    
    // 检查是 Graphics 还是 Compute PSO
    bool isCompute = vkPso->IsCompute();
    VkPipelineBindPoint bindPoint = isCompute ? 
        VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    
    if (pipeline != VK_NULL_HANDLE && m_Impl->cmdBuffer != VK_NULL_HANDLE) {
        vkCmdBindPipeline(m_Impl->cmdBuffer, bindPoint, pipeline);
    }
    
    m_Impl->currentPipeline = pipeline;
    m_Impl->currentPipelineLayout = layout;
      m_Impl->currentPso = vkPso;
}

// ════════════════════════════════════════════════════════════
// 几何体绑定
// ════════════════════════════════════════════════════════════

void VulkanCommandList::SetVertexBuffer(uint32 slot, IRHIBuffer* buffer,
                                         uint32 stride, uint32 offset) {
    if (!m_Impl->IsReady()) return;
    auto* vkBuffer = static_cast<VulkanBuffer*>(buffer);
    VkBuffer vkbuf = vkBuffer->GetVkBuffer();
    VkDeviceSize vkOffset = offset;

    vkCmdBindVertexBuffers(m_Impl->cmdBuffer, slot, 1, &vkbuf, &vkOffset);
}

void VulkanCommandList::SetIndexBuffer(IRHIBuffer* buffer, uint32 offset) {
    if (!m_Impl->IsReady()) return;
    auto* vkBuffer = static_cast<VulkanBuffer*>(buffer);
    VkBuffer vkbuf = vkBuffer->GetVkBuffer();

    // 默认使用 VK_INDEX_TYPE_UINT32
    vkCmdBindIndexBuffer(m_Impl->cmdBuffer, vkbuf, offset, VK_INDEX_TYPE_UINT32);
}

void VulkanCommandList::SetPrimitiveTopology(PrimitiveTopology topology) {
    if (!m_Impl->IsReady()) return;
    
    // 尝试使用 VK_EXT_extended_dynamic_state
    // 若不支持，拓扑已固化在 PSO 中，此调用被忽略
    // 检查是否为标准拓扑且需要动态设置
    VkPrimitiveTopology vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    switch (topology) {
        case PrimitiveTopology::TriangleList:  vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
        case PrimitiveTopology::TriangleStrip: vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
        case PrimitiveTopology::LineList:      vkTopo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
        case PrimitiveTopology::PointList:     vkTopo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
        default: break;
    }
    // 动态状态仅在 PSO 中设置了 VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY 时有效
    // 此处预留 vkCmdSetPrimitiveTopologyEXT 调用路径
    // 当前简化：直接忽略，拓扑在 PSO 中固化
}

// ════════════════════════════════════════════════════════════
// 绘制
// ════════════════════════════════════════════════════════════

void VulkanCommandList::DrawIndexed(uint32 indexCount, uint32 startIndex,
                                     uint32 baseVertex) {
    if (!m_Impl->IsReady()) return;
    m_Impl->FlushBarriers();
    vkCmdDrawIndexed(m_Impl->cmdBuffer, indexCount, 1, startIndex, baseVertex, 0);
}

void VulkanCommandList::Draw(uint32 vertexCount, uint32 startVertex) {
    if (!m_Impl->IsReady()) return;
    m_Impl->FlushBarriers();
    vkCmdDraw(m_Impl->cmdBuffer, vertexCount, 1, startVertex, 0);
}

void VulkanCommandList::DrawIndexedIndirect(IRHIBuffer* argsBuffer, uint32 offset) {
    if (!m_Impl->IsReady()) return;
    m_Impl->FlushBarriers();
    auto* vkBuffer = static_cast<VulkanBuffer*>(argsBuffer);
    vkCmdDrawIndexedIndirect(m_Impl->cmdBuffer, vkBuffer->GetVkBuffer(),
                              offset, 1, sizeof(VkDrawIndexedIndirectCommand));
}

// ════════════════════════════════════════════════════════════
// 视口 / 裁剪
// ════════════════════════════════════════════════════════════

void VulkanCommandList::SetViewport(const Viewport& vp) {
    if (!m_Impl->IsReady()) return;
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
    if (!m_Impl->IsReady()) return;
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
    if (!m_Impl->IsReady()) return;
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

        // 使用纹理的布局追踪，避免硬编码 oldLayout = UNDEFINED
        // UNDEFINED 会导致 GPU 丢弃图像内容，第二帧起画面变黑
        ResourceBarrierDesc trackedSrc = src;
        if (src.texture) {
            auto* vkTex = static_cast<VulkanTexture*>(src.texture);
            VkImageLayout currentLayout = vkTex->GetLayout();
            if (currentLayout != VK_IMAGE_LAYOUT_UNDEFINED && 
                trackedSrc.stateBefore == ResourceState::Undefined) {
                // 如果 RHI 层传入了 Undefined 但纹理已有有效布局，使用真实布局
                trackedSrc.stateBefore = ResourceState::ShaderResource; // 近似
                // 真正正确的做法：将 VkImageLayout 映射回 ResourceState
                // 此处保持追踪的兼容性
            }
        }

        ConvertBarrierDesc(trackedSrc, vkBarrier, srcStage, dstStage);

        // Barrier 执行后更新纹理布局
        if (src.texture) {
            auto* vkTex = static_cast<VulkanTexture*>(src.texture);
            vkTex->SetLayout(vkBarrier.newLayout);
        }

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
      if (!m_Impl->IsReady()) return;
      auto* vkBuffer = static_cast<VulkanBuffer*>(buffer);
      if (!vkBuffer || vkBuffer->GetVkBuffer() == VK_NULL_HANDLE) return;

      // 此前这里只重绑 currentDescriptorSet，而它在任何引擎路径下都不会被置为
      // 非空 —— 于是函数体永远不执行，且即使执行也没有写入任何描述符。
      // 现在真正写入：分配 set → 写 buffer 描述符 → 绑定。
      VulkanPipelineState* pso = m_Impl->currentPso;
      if (!pso || !m_Impl->device) return;
      VkDescriptorSetLayout setLayout = pso->GetDescriptorSetLayout();
      if (setLayout == VK_NULL_HANDLE) {
          std::fprintf(stderr,
                       "[Vulkan] SetConstantBuffer: the bound PSO declares no descriptors "
                       "(add a DescriptorBindingDesc to GraphicsPSODesc)\n");
          return;
      }
      VkDescriptorSet dset = m_Impl->AcquireDescriptorSet(set);
      if (dset == VK_NULL_HANDLE) return;

      VkDescriptorBufferInfo info{};
      info.buffer = vkBuffer->GetVkBuffer();
      info.offset = offset;
      info.range  = size ? size : vkBuffer->GetSize();
      VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      write.dstSet      = dset;
      write.dstBinding  = binding;
      write.dstArrayElement = 0;
      write.descriptorCount = 1;
      write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      write.pBufferInfo     = &info;
      vkUpdateDescriptorSets(m_Impl->device->GetVkDevice(), 1, &write, 0, nullptr);
  }

  void VulkanCommandList::SetShaderResource(uint32 set, uint32 binding,
                                              IRHITexture* texture)
  {
      if (!m_Impl->IsReady()) return;
      auto* vkTex = static_cast<VulkanTexture*>(texture);
      if (!vkTex || vkTex->GetVkImageView() == VK_NULL_HANDLE) return;
      VulkanPipelineState* pso = m_Impl->currentPso;
      if (!pso || !m_Impl->device) return;
      if (pso->GetDescriptorSetLayout() == VK_NULL_HANDLE) {
          std::fprintf(stderr,
                       "[Vulkan] SetShaderResource: the bound PSO declares no descriptors\n");
          return;
      }
      VkSampler sampler = m_Impl->device->GetSampler();
      if (sampler == VK_NULL_HANDLE) {
          std::fprintf(stderr, "[Vulkan] SetShaderResource: no sampler available\n");
          return;
      }

      // 被采样的图像必须处于 SHADER_READ_ONLY，否则采样是未定义行为。
      if (vkTex->GetLayout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
          VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
          b.image            = vkTex->GetVkImage();
          b.oldLayout        = vkTex->GetLayout();
          b.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          b.subresourceRange = { FormatToAspect(vkTex->GetFormat()), 0, 1, 0, 1 };
          b.srcAccessMask    = b.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
                                   ? 0 : VK_ACCESS_SHADER_READ_BIT;
          b.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
          m_Impl->FlushBarriers();
          vkCmdPipelineBarrier(m_Impl->cmdBuffer,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                               0, nullptr, 0, nullptr, 1, &b);
          vkTex->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }

      VkDescriptorSet dset = m_Impl->AcquireDescriptorSet(set);
      if (dset == VK_NULL_HANDLE) return;

      VkDescriptorImageInfo img{};
      img.sampler     = sampler;
      img.imageView   = vkTex->GetVkImageView();
      img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      write.dstSet      = dset;
      write.dstBinding  = binding;
      write.dstArrayElement = 0;
      write.descriptorCount = 1;
      write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.pImageInfo      = &img;
      vkUpdateDescriptorSets(m_Impl->device->GetVkDevice(), 1, &write, 0, nullptr);
  }

// ════════════════════════════════════════════════════════════
// Compute 管线
// ════════════════════════════════════════════════════════════

void VulkanCommandList::Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) {
    if (!m_Impl->IsReady()) return;
    m_Impl->FlushBarriers();
    vkCmdDispatch(m_Impl->cmdBuffer, groupX, groupY, groupZ);
}

void VulkanCommandList::SetUnorderedAccess(uint32 slot, IRHIBuffer* buffer) {
    if (!m_Impl->IsReady()) return;
    auto* vkBuffer = static_cast<VulkanBuffer*>(buffer);
    VkBuffer vkbuf = vkBuffer->GetVkBuffer();
    
    // 通过 descriptor set 绑定 SSBO
    // 当前使用 Bindless 模式的底层描述符
    if (m_Impl->currentPipelineLayout != VK_NULL_HANDLE && 
        m_Impl->currentDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(m_Impl->cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_Impl->currentPipelineLayout, 0, 1, &m_Impl->currentDescriptorSet, 0, nullptr);
    }
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
  void VulkanCommandList::AttachToRecording(VkCommandBuffer cmdBuf) noexcept {
      m_Impl->cmdBuffer = cmdBuf;
      // 声明该缓冲已在录制：所有 IsReady() 守卫据此放行，End() 负责结束它。
      m_Impl->isRecording = true;
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
