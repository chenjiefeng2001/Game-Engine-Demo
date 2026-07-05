/**
 * @file VulkanSwapChain.cpp
 * @brief Vulkan 交换链实现
 */

#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Vulkan/VulkanCommon.h"

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// VulkanSwapChain::Impl
// ════════════════════════════════════════════════════════════

struct VulkanSwapChain::Impl {
    VulkanDevice* device{nullptr};
    uint32_t currentImageIndex{0};
    uint32_t bufferCount{3};
};

VulkanSwapChain::VulkanSwapChain() : m_Impl(std::make_unique<Impl>()) {}
VulkanSwapChain::~VulkanSwapChain() = default;

void VulkanSwapChain::Present() {
    // 实际呈现逻辑在 VulkanDevice 的渲染循环中
    // 这里简化实现
}

void VulkanSwapChain::Resize(uint32_t w, uint32_t h) {
    (void)w;
    (void)h;
}

IRHITexture* VulkanSwapChain::GetBackBuffer(uint32_t idx) const {
    (void)idx;
    return nullptr;
}

uint32_t VulkanSwapChain::GetCurrentBackBufferIndex() const {
    return 0;
}

uint32_t VulkanSwapChain::GetBufferCount() const {
    return m_Impl->bufferCount;
}

} // namespace RHI
} // namespace Engine