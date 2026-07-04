/**
 * @file VulkanPipelineState.cpp
 * @brief Vulkan Pipeline State 实现（通过 PSOCache 管理）
 */

#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Vulkan/VulkanCommon.h"

namespace Engine {
namespace RHI {

struct VulkanPipelineState::Impl {
    VkPipeline       pipeline{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
    bool             isCompute{false};
};

VulkanPipelineState::VulkanPipelineState() : m_Impl(std::make_unique<Impl>()) {}
VulkanPipelineState::~VulkanPipelineState() = default;

} // namespace RHI
} // namespace Engine