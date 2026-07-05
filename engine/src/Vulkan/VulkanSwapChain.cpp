/**
 * @file VulkanSwapChain.cpp
 * @brief Vulkan 交换链实现 — 完整的 Present/Resize/GetBackBuffer
 */

#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Vulkan/VulkanCommon.h"

namespace Engine {
namespace RHI {

struct VulkanSwapChain::Impl {
    VulkanDevice*       device{nullptr};
    VkSwapchainKHR      swapChain{VK_NULL_HANDLE};
    VkSurfaceKHR        surface{VK_NULL_HANDLE};
    uint32_t            currentImageIndex{0};
    uint32_t            bufferCount{3};

    // 交换链图像
    std::vector<VkImage>        images;
    std::vector<VulkanTexture*> backBufferTextures;

    uint32_t width{1280};
    uint32_t height{720};
    VkFormat format{VK_FORMAT_B8G8R8A8_UNORM};
    VkPresentModeKHR presentMode{VK_PRESENT_MODE_FIFO_KHR};

    ~Impl() {
        for (auto* tex : backBufferTextures) delete tex;
        backBufferTextures.clear();
    }
};

VulkanSwapChain::VulkanSwapChain() : m_Impl(std::make_unique<Impl>()) {}
VulkanSwapChain::~VulkanSwapChain() = default;

void VulkanSwapChain::Present() {
    if (!m_Impl->device || m_Impl->swapChain == VK_NULL_HANDLE) return;

    VkDevice dev = m_Impl->device->GetVkDevice();
    VkQueue queue = m_Impl->device->GetGraphicsQueue();

    auto& frame = m_Impl->device->GetFrameContext();
    VulkanFrameResource& fr = frame.frames[frame.currentFrame];

    // 等待帧 fence
    vkWaitForFences(dev, 1, &fr.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(dev, 1, &fr.fence);

    // 获取下一张图像
    VkResult result = vkAcquireNextImageKHR(dev, m_Impl->swapChain, UINT64_MAX,
        fr.imageAvailable, VK_NULL_HANDLE, &m_Impl->currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // 交换链过期，跳过这一帧
        return;
    }

    // 重置命令缓冲
    vkResetCommandBuffer(fr.commandBuffer, 0);

    // 提交渲染命令
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

    // 呈现
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &fr.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_Impl->swapChain;
    presentInfo.pImageIndices = &m_Impl->currentImageIndex;

    vkQueuePresentKHR(queue, &presentInfo);

    // 推进帧索引
    frame.currentFrame = (frame.currentFrame + 1) % frame.kMaxFramesInFlight;
}

void VulkanSwapChain::Resize(uint32_t w, uint32_t h) {
    if (!m_Impl->device) return;

    VkDevice dev = m_Impl->device->GetVkDevice();

    // 等待设备空闲
    vkDeviceWaitIdle(dev);

    // 销毁旧的后备缓冲纹理
    for (auto* tex : m_Impl->backBufferTextures) delete tex;
    m_Impl->backBufferTextures.clear();

    m_Impl->width = w;
    m_Impl->height = h;

    // 重建交换链
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = m_Impl->surface;
    ci.minImageCount = m_Impl->bufferCount;
    ci.imageFormat = m_Impl->format;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = {w, h};
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = m_Impl->presentMode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = m_Impl->swapChain;

    VkSwapchainKHR newSwapChain = VK_NULL_HANDLE;
    VkResult result = vkCreateSwapchainKHR(dev, &ci, nullptr, &newSwapChain);
    if (result != VK_SUCCESS) return;

    // 销毁旧的交换链
    if (m_Impl->swapChain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(dev, m_Impl->swapChain, nullptr);
    m_Impl->swapChain = newSwapChain;

    // 获取新图像
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(dev, m_Impl->swapChain, &imageCount, nullptr);
    m_Impl->images.resize(imageCount);
    vkGetSwapchainImagesKHR(dev, m_Impl->swapChain, &imageCount, m_Impl->images.data());

    // 创建后备缓冲纹理包装
    for (auto vkImage : m_Impl->images) {
        auto* tex = new VulkanTexture();
        tex->SetVkImage(vkImage);
        tex->SetWidth(w);
        tex->SetHeight(h);
        tex->SetFormat(VkToFormat(m_Impl->format));
        m_Impl->backBufferTextures.push_back(tex);
    }

    m_Impl->bufferCount = imageCount;
}

IRHITexture* VulkanSwapChain::GetBackBuffer(uint32_t idx) const {
    if (idx >= m_Impl->backBufferTextures.size()) return nullptr;
    return m_Impl->backBufferTextures[idx];
}

uint32_t VulkanSwapChain::GetCurrentBackBufferIndex() const {
    return m_Impl->currentImageIndex;
}

uint32_t VulkanSwapChain::GetBufferCount() const {
    return m_Impl->bufferCount;
}

} // namespace RHI
} // namespace Engine