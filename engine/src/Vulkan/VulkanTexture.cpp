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
    VkImageLayout currentLayout{VK_IMAGE_LAYOUT_UNDEFINED};

    ~Impl() {
        if (vkImage != VK_NULL_HANDLE && vmaAllocator != nullptr) {
            vmaDestroyImage(vmaAllocator, vkImage, allocation);
        }
        if (defaultView != VK_NULL_HANDLE) {
            // 需要 VkDevice 来销毁 ImageView
            // 这里由外部管理
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

} // namespace RHI
} // namespace Engine
