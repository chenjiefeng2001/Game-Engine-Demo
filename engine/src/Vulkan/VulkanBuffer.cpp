/**
 * @file VulkanBuffer.cpp
 * @brief Vulkan Buffer 实现（VMA 持久映射）
 */

#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Vulkan/VulkanCommon.h"

#include <cstring>

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// VulkanBuffer::Impl
// ════════════════════════════════════════════════════════════

struct VulkanBuffer::Impl {
    VkBuffer      vkBuffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    GPUAllocation gpuAlloc;
    uint64_t      size{0};

    VmaAllocator  vmaAllocator{nullptr};

    ~Impl() {
        if (vkBuffer != VK_NULL_HANDLE && vmaAllocator != nullptr) {
            vmaDestroyBuffer(vmaAllocator, vkBuffer, allocation);
        }
    }
};

VulkanBuffer::VulkanBuffer() : m_Impl(std::make_unique<Impl>()) {}
VulkanBuffer::~VulkanBuffer() = default;

uint64_t VulkanBuffer::GetSize() const noexcept {
    return m_Impl->size;
}

const GPUAllocation& VulkanBuffer::GetAllocation() const noexcept {
    return m_Impl->gpuAlloc;
}

VkBuffer VulkanBuffer::GetVkBuffer() const noexcept {
    return m_Impl->vkBuffer;
}

void VulkanBuffer::SetAllocation(const GPUAllocation& alloc) {
    m_Impl->gpuAlloc = alloc;
}

void VulkanBuffer::SetVkBuffer(VkBuffer buffer) {
    m_Impl->vkBuffer = buffer;
}

void VulkanBuffer::SetSize(uint64_t size) {
    m_Impl->size = size;
}

} // namespace RHI
} // namespace Engine