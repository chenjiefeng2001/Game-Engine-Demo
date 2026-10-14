/**
 * @file VulkanSwapChain.cpp
 * @brief Vulkan 交换链实现 — 完整的 Present/Resize/GetBackBuffer
 *
 * 整体架构修复（2025-07-07）：
 *
 * 正确的三缓冲流程（每帧调用 Present 一次）：
 *
 *   [Present 内]：
 *   1. vkWaitForFences(fr.fence)     ← 等待本槽位上一帧 GPU 完成
 *   2. vkResetFences(fr.fence)
 *   3. vkAcquireNextImageKHR         ← 获取交换链图像（信号 fr.imageAvailable）
 *   4. vkQueueSubmit(用户命令)       ← 提交用户已录制的命令
 *      - 等待: fr.imageAvailable（图像可用后才开始渲染）
 *      - 信号: fr.renderFinished（渲染完成）
 *      - 围栏: fr.fence（GPU 处理完本槽位时触发）
 *   5. vkQueuePresentKHR             ← 呈现
 *      - 等待: fr.renderFinished（确保渲染已完成）
 *   6. 推进 frameCtx.currentFrame
 *
 *   [用户需在 Present 前录制]：
 *   - VulkanDevice::CreateCommandList() 返回当前 frame 的 command buffer
 *   - 用户在命令开头必须插入 PRESENT_SRC → COLOR_ATTACHMENT_OPTIMAL Barrier
 *   - 然后录制渲染命令
 *   - 调用 End() 关闭 command buffer
 *
 *   [关键修复]：
 *   - X 不再在 Present 中调用 vkResetCommandBuffer（这会擦掉用户已录制的命令）
 *   - X 不再在 Present 末尾额外提交空信号（Fence 已由 vkQueueSubmit 隐含触发）
 *   - ✅ 使用 fr.renderFinished 信号量同步 Present 等待
 */

#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Vulkan/VulkanCommon.h"

#include <cstdio>

namespace Engine {
namespace RHI {

VulkanSwapChain::VulkanSwapChain() = default;

VulkanSwapChain::~VulkanSwapChain() {
    // 交换链**句柄**的所有权在 VulkanDevice：它由 VulkanDevice::CreateSwapChain 创建
    // 并存进 m_Impl->swapChain，Shutdown() 会销毁它。这里再销毁一次就是双重释放，
    // 在 X11 实测中表现为 free(): double free detected。
    //
    // backbuffer 的 image view 归各自的 VulkanTexture 所有（shared_ptr，成员释放时
    // 触发），本类无需处理。
}

void VulkanSwapChain::Present() {
    if (!m_Device || m_SwapChain == VK_NULL_HANDLE) return;
    if (!m_ImageAcquired) {
        std::fprintf(stderr, "[VulkanSwapChain] Present without AcquireNextImage\n");
        return;
    }

    VkDevice dev = m_Device->GetVkDevice();
    VkQueue queue = m_Device->GetGraphicsQueue();

    auto& frameCtx = m_Device->GetFrameContext();
    VulkanFrameResource& fr = frameCtx.frames[frameCtx.currentFrame];
    const uint32_t imageIndex = m_ImageIndex;

    // ── 3. 提交调用方已录制的命令（等待 fr.imageAvailable，由 AcquireNextImage 触发）──
    VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &fr.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &fr.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &fr.renderFinished;
    vkQueueSubmit(queue, 1, &submitInfo, fr.fence);

    // ── 4. 呈现图像 ──
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &fr.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_SwapChain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult result = vkQueuePresentKHR(queue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "[VulkanSwapChain] Present returned %d\n", result);
    }

    // ── 5. 推进到下一帧 ──
    m_ImageAcquired = false;
    frameCtx.currentFrame = (frameCtx.currentFrame + 1) % frameCtx.kMaxFramesInFlight;
}

uint32_t VulkanSwapChain::AcquireNextImage() {
    if (!m_Device || m_SwapChain == VK_NULL_HANDLE) return UINT32_MAX;
    if (m_ImageAcquired) return m_ImageIndex;   // 已 acquire，重复调用返回同一索引

    VkDevice dev = m_Device->GetVkDevice();
    VkQueue queue = m_Device->GetGraphicsQueue();
    auto& frameCtx = m_Device->GetFrameContext();

    // 复用帧槽位前必须等上一轮 GPU 工作完成，否则会覆写仍在使用的命令缓冲与信号量。
    VulkanFrameResource& fr = frameCtx.frames[frameCtx.currentFrame];
    vkWaitForFences(dev, 1, &fr.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(dev, 1, &fr.fence);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(dev, m_SwapChain, UINT64_MAX,
        fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "[VulkanSwapChain] vkAcquireNextImageKHR returned %d\n", result);
        return UINT32_MAX;
    }
    m_ImageIndex = imageIndex;
    m_ImageAcquired = true;
    return imageIndex;
}

void VulkanSwapChain::Resize(uint32_t w, uint32_t h) {
    if (!m_Device) return;
    VkDevice dev = m_Device->GetVkDevice();
    // 窗口 Resize 必须排空管线：唯一正确的 vkDeviceWaitIdle 使用场景
    vkDeviceWaitIdle(dev);
    m_BackBuffers.clear();
    m_SwapChainImages.clear();
}

void VulkanSwapChain::SetSwapChainImages(const std::vector<VkImage>& images) {
    m_SwapChainImages = images;
}

IRHITexture* VulkanSwapChain::GetBackBuffer(uint32_t idx) const {
    if (idx >= m_BackBuffers.size()) return nullptr;
    return m_BackBuffers[idx].get();
}

uint32_t VulkanSwapChain::GetCurrentBackBufferIndex() const { return m_ImageIndex; }
uint32_t VulkanSwapChain::GetBufferCount() const { return (uint32_t)m_BackBuffers.size(); }

} // namespace RHI
} // namespace Engine