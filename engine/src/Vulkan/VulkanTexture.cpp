/**
 * @file VulkanTexture.cpp
 * @brief Vulkan Texture 实现（VMA 分配）
 */

#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Vulkan/VulkanCommon.h"

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// VulkanTexture::Impl
// ════════════════════════════════════════════════════════════

struct VulkanTexture::Impl {
    VkImage       vkImage{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkImageView   defaultView{VK_NULL_HANDLE};
    uint32_t      width{0};
    uint32_t      height{0};
    Format        format{Format::Unknown};
    VmaAllocator  vmaAllocator{nullptr};
    VkDevice      device{nullptr};          ///< 用于销毁 defaultView
    VkImageLayout currentLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    bool          swapChainImage{false};    ///< 交换链 backbuffer，见头文件说明

    ~Impl() {
        // 顺序不能反：view 引用着 image，必须先销毁 view。
        if (defaultView != VK_NULL_HANDLE && device != nullptr) {
            vkDestroyImageView(device, defaultView, nullptr);
            defaultView = VK_NULL_HANDLE;
        }
        if (vkImage != VK_NULL_HANDLE && vmaAllocator != nullptr) {
            vmaDestroyImage(vmaAllocator, vkImage, allocation);
        }
    }
};

VulkanTexture::VulkanTexture() : m_Impl(std::make_unique<Impl>()) {}
VulkanTexture::~VulkanTexture() = default;

uint32_t VulkanTexture::GetWidth() const noexcept {
    return m_Impl->width;
}

uint32_t VulkanTexture::GetHeight() const noexcept {
    return m_Impl->height;
}

Format VulkanTexture::GetFormat() const noexcept {
    return m_Impl->format;
}

VkImage VulkanTexture::GetVkImage() const noexcept {
    return m_Impl->vkImage;
}

void VulkanTexture::SetVkImage(VkImage image) {
    m_Impl->vkImage = image;
}

VkImageView VulkanTexture::GetVkImageView() const noexcept {
    return m_Impl->defaultView;
}

void VulkanTexture::SetVkImageView(VkImageView view) {
    m_Impl->defaultView = view;
}

void VulkanTexture::SetVkDevice(VkDevice device) {
    m_Impl->device = device;
}

void VulkanTexture::SetWidth(uint32_t w) {
    m_Impl->width = w;
}

void VulkanTexture::SetHeight(uint32_t h) {
    m_Impl->height = h;
}

void VulkanTexture::SetFormat(Format fmt) {
    m_Impl->format = fmt;
}

void VulkanTexture::SetAllocator(VmaAllocator allocator) {
    m_Impl->vmaAllocator = allocator;
}

void VulkanTexture::SetAllocation(VmaAllocation alloc) {
    m_Impl->allocation = alloc;
}

void VulkanTexture::SetLayout(VkImageLayout layout) noexcept {
    m_Impl->currentLayout = layout;
}

VkImageLayout VulkanTexture::GetLayout() const noexcept {
    return m_Impl->currentLayout;
}

void VulkanTexture::SetIsSwapChainImage(bool value) noexcept {
    m_Impl->swapChainImage = value;
}

bool VulkanTexture::IsSwapChainImage() const noexcept {
    return m_Impl->swapChainImage;
}

} // namespace RHI
} // namespace Engine
