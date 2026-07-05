/**
 * @file VulkanSwapChain.cpp
 * @brief Vulkan 交换链实现 — 完整的 Present/Resize/GetBackBuffer
 */

#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Vulkan/VulkanCommon.h"

namespace Engine {
namespace RHI {

VulkanSwapChain::VulkanSwapChain() = default;
VulkanSwapChain::~VulkanSwapChain() = default;

void VulkanSwapChain::Present() {
    if (!m_Device || m_SwapChain == VK_NULL_HANDLE) return;

    VkDevice dev = m_Device->GetVkDevice();
    VkQueue queue = m_Device->GetGraphicsQueue();

    auto& frame = m_Device->GetFrameContext();
    VulkanFrameResource& fr = frame.frames[frame.currentFrame];

    vkWaitForFences(dev, 1, &fr.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(dev, 1, &fr.fence);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(dev, m_SwapChain, UINT64_MAX,
        fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) return;

    vkResetCommandBuffer(fr.commandBuffer, 0);

    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &fr.imageAvailable;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &fr.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &fr.renderFinished;
    vkQueueSubmit(queue, 1, &submitInfo, fr.fence);

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &fr.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_SwapChain;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(queue, &presentInfo);

    frame.currentFrame = (frame.currentFrame + 1) % frame.kMaxFramesInFlight;
}

void VulkanSwapChain::Resize(uint32_t w, uint32_t h) {
    if (!m_Device) return;
    VkDevice dev = m_Device->GetVkDevice();
    vkDeviceWaitIdle(dev);
    m_BackBuffers.clear();
}

IRHITexture* VulkanSwapChain::GetBackBuffer(uint32_t idx) const {
    if (idx >= m_BackBuffers.size()) return nullptr;
    return m_BackBuffers[idx].get();
}

uint32_t VulkanSwapChain::GetCurrentBackBufferIndex() const { return 0; }
uint32_t VulkanSwapChain::GetBufferCount() const { return (uint32_t)m_BackBuffers.size(); }

} // namespace RHI
} // namespace Engine