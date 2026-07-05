#pragma once

/**
 * @file VulkanCommon.h
 * @brief Vulkan 公共辅助函数和常量
 */

#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RHI/RenderCommand.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RenderResources/ShaderStage.h"
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <vector>
#include <string>

namespace Engine {

// ── 调试宏 ──
#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult r = (x); if (r != VK_SUCCESS) { \
    std::fprintf(stderr, "[Vulkan] VK_CHECK failed at %s:%d: %d\n", __FILE__, __LINE__, (int)r); \
    __debugbreak(); \
} } while(0)
#endif

// ── 常量 ──
constexpr uint32_t kMaxFramesInFlight = 3;
constexpr uint32_t kMaxDescriptorCount = 1024;
constexpr uint32_t kDynamicUBOSizePerFrame = 64 * 1024;  // 64KB per frame
constexpr VkCommandPoolCreateFlags kThreadCmdPoolFlags =
    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
    VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

// ── 格式转换 ──
VkFormat FormatToVk(RHI::Format format);
RHI::Format VkToFormat(VkFormat format);

// ── 屏障辅助 ──
void ConvertBarrierDesc(
    const RHI::ResourceBarrierDesc& src,
    VkImageMemoryBarrier& outBarrier,
    VkPipelineStageFlags& outSrcStage,
    VkPipelineStageFlags& outDstStage);

// ── 实例/设备扩展 ──
std::vector<const char*> GetRequiredInstanceExtensions(bool enableValidation);
std::vector<const char*> GetRequiredDeviceExtensions();

// ── 着色器阶段转换 ──
VkShaderStageFlagBits ShaderStageToVk(ShaderStage stage);

// ── 拓扑转换 ──
VkPrimitiveTopology TopologyToVk(RHI::PrimitiveTopology topology);

// ── 格式大小 ──
uint32_t GetFormatSize(RHI::Format format);

// ── 资源状态转换（Vulkan 专用，在 VulkanCommon.cpp 中实现） ──
VkImageLayout ResourceStateToLayout(RHI::ResourceState state) noexcept;
VkPipelineStageFlags ResourceStateToStage(RHI::ResourceState state) noexcept;
VkAccessFlags ResourceStateToAccess(RHI::ResourceState state) noexcept;

} // namespace Engine