/**
 * @file VulkanCommon.cpp
 * @brief Vulkan ĺąäşŤçąťĺć ĺ°ä¸čžĺŠĺ˝ć°ĺŽç?
 */

#include "Engine/Vulkan/VulkanCommon.h"

#ifndef ENGINE_HAS_VULKAN
#define ENGINE_HAS_VULKAN 1
#endif

#include <cstdio>
#include <cstdlib>

namespace Engine {

using namespace RHI;

// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ
// Format â?VkFormat
// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ

VkFormat FormatToVk(RHI::Format fmt) {
    switch (fmt) {
        case Format::R8_UNorm:     return VK_FORMAT_R8_UNORM;
        case Format::R8_SNorm:     return VK_FORMAT_R8_SNORM;
        case Format::R8_UInt:      return VK_FORMAT_R8_UINT;
        case Format::R8_SInt:      return VK_FORMAT_R8_SINT;
        case Format::R16_UNorm:    return VK_FORMAT_R16_UNORM;
        case Format::R16_SNorm:    return VK_FORMAT_R16_SNORM;
        case Format::R16_UInt:     return VK_FORMAT_R16_UINT;
        case Format::R16_SInt:     return VK_FORMAT_R16_SINT;
        case Format::R16_Float:    return VK_FORMAT_R16_SFLOAT;
        case Format::RG8_UNorm:    return VK_FORMAT_R8G8_UNORM;
        case Format::R32_UInt:     return VK_FORMAT_R32_UINT;
        case Format::R32_SInt:     return VK_FORMAT_R32_SINT;
        case Format::R32_Float:    return VK_FORMAT_R32_SFLOAT;
        case Format::RGBA8_UNorm:  return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_sRGB:   return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::BGRA8_UNorm:  return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRA8_sRGB:   return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::RGBA16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::RGBA32_Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::D32_Float:    return VK_FORMAT_D32_SFLOAT;
        case Format::D24_UNorm_S8_UInt: return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32_Float_S8_UInt: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        default: return VK_FORMAT_UNDEFINED;
    }
}

RHI::Format VkToFormat(VkFormat fmt) {
    switch (fmt) {
        case VK_FORMAT_R8_UNORM:         return Format::R8_UNorm;
        case VK_FORMAT_R8G8_UNORM:       return Format::RG8_UNorm;
        case VK_FORMAT_R8G8B8A8_UNORM:   return Format::RGBA8_UNorm;
        case VK_FORMAT_B8G8R8A8_UNORM:   return Format::BGRA8_UNorm;
        case VK_FORMAT_R32_SFLOAT:       return Format::R32_Float;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::RGBA32_Float;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::RGBA16_Float;
        case VK_FORMAT_D32_SFLOAT:       return Format::D32_Float;
        case VK_FORMAT_D24_UNORM_S8_UINT: return Format::D24_UNorm_S8_UInt;
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return Format::D32_Float_S8_UInt;
        default: return Format::Unknown;
    }
}

// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ
// čľćşçść?â?Vulkan ćä¸ž
// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ

VkImageLayout ResourceStateToLayout(RHI::ResourceState state) noexcept {
    switch (state) {
        case RHI::ResourceState::Undefined:      return VK_IMAGE_LAYOUT_UNDEFINED;
        case RHI::ResourceState::RenderTarget:   return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RHI::ResourceState::DepthStencil:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case RHI::ResourceState::ShaderResource: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RHI::ResourceState::CopySource:     return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case RHI::ResourceState::CopyDest:       return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RHI::ResourceState::Present:        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        case RHI::ResourceState::UnorderedAccess: return VK_IMAGE_LAYOUT_GENERAL;
        default: return VK_IMAGE_LAYOUT_GENERAL;
    }
}

VkPipelineStageFlags ResourceStateToStage(RHI::ResourceState state) noexcept {
    uint16_t s = static_cast<uint16_t>(state);
    VkPipelineStageFlags flags = 0;

    switch (state) {
        case RHI::ResourceState::Undefined:
            flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
        case RHI::ResourceState::RenderTarget:
            flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case RHI::ResourceState::DepthStencil:
            flags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;
        case RHI::ResourceState::ShaderResource:
            flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            break;
        case RHI::ResourceState::UnorderedAccess:
            flags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            break;
        case RHI::ResourceState::CopySource:
        case RHI::ResourceState::CopyDest:
            flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case RHI::ResourceState::Present:
            flags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
        case RHI::ResourceState::Common:
            flags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            break;
        default:
            if (s & static_cast<uint16_t>(RHI::ResourceState::RenderTarget))
                flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            if (s & static_cast<uint16_t>(RHI::ResourceState::DepthStencil))
                flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            if (s & static_cast<uint16_t>(RHI::ResourceState::ShaderResource))
                flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            if (s & static_cast<uint16_t>(RHI::ResourceState::UnorderedAccess))
                flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            if (s & static_cast<uint16_t>(RHI::ResourceState::CopySource) || s & static_cast<uint16_t>(RHI::ResourceState::CopyDest))
                flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            if (s & static_cast<uint16_t>(RHI::ResourceState::VertexBuffer) || s & static_cast<uint16_t>(RHI::ResourceState::IndexBuffer) || s & static_cast<uint16_t>(RHI::ResourceState::ConstantBuffer))
                flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            if (s & static_cast<uint16_t>(RHI::ResourceState::Present))
                flags |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
    }

    if (flags == 0) flags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    return flags;
}

VkAccessFlags ResourceStateToAccess(RHI::ResourceState state) noexcept {
    uint16_t s = static_cast<uint16_t>(state);
    VkAccessFlags flags = 0;
    if (s & static_cast<uint16_t>(RHI::ResourceState::RenderTarget))
        flags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (s & static_cast<uint16_t>(RHI::ResourceState::DepthStencil))
        flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (s & static_cast<uint16_t>(RHI::ResourceState::ShaderResource))
        flags |= VK_ACCESS_SHADER_READ_BIT;
    if (s & static_cast<uint16_t>(RHI::ResourceState::UnorderedAccess))
        flags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    if (s & static_cast<uint16_t>(RHI::ResourceState::CopySource))
        flags |= VK_ACCESS_TRANSFER_READ_BIT;
    if (s & static_cast<uint16_t>(RHI::ResourceState::CopyDest))
        flags |= VK_ACCESS_TRANSFER_WRITE_BIT;
    if (s & static_cast<uint16_t>(RHI::ResourceState::VertexBuffer))
        flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (s & static_cast<uint16_t>(RHI::ResourceState::IndexBuffer))
        flags |= VK_ACCESS_INDEX_READ_BIT;
    if (s & static_cast<uint16_t>(RHI::ResourceState::ConstantBuffer))
        flags |= VK_ACCESS_UNIFORM_READ_BIT;
    if (flags == 0)
        flags = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    return flags;
}

void ConvertBarrierDesc(
    const ResourceBarrierDesc& src,
    VkImageMemoryBarrier& outBarrier,
    VkPipelineStageFlags& outSrcStage,
    VkPipelineStageFlags& outDstStage)
{
    outBarrier = {};
    outBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    outBarrier.oldLayout = ResourceStateToLayout(src.stateBefore);
    outBarrier.newLayout = ResourceStateToLayout(src.stateAfter);
    outBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    outBarrier.subresourceRange.baseMipLevel = 0;
    outBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    outBarrier.subresourceRange.baseArrayLayer = 0;
    outBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    outSrcStage = ResourceStateToStage(src.stateBefore);
    outDstStage = ResourceStateToStage(src.stateAfter);
    outBarrier.srcAccessMask = ResourceStateToAccess(src.stateBefore);
    outBarrier.dstAccessMask = ResourceStateToAccess(src.stateAfter);
}

// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ
// çč˛ĺ¨éśćŽľč˝Źć˘
// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ

VkShaderStageFlagBits ShaderStageToVk(ShaderStage stage) {
    switch (stage.type) {
        case ShaderStageType::Vertex:   return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStageType::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStageType::Compute:  return VK_SHADER_STAGE_COMPUTE_BIT;
        case ShaderStageType::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStageType::TessControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStageType::TessEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        default: return VK_SHADER_STAGE_ALL;
    }
}

// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ
// ććč˝Źć˘
// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ

VkPrimitiveTopology TopologyToVk(RHI::PrimitiveTopology topology) {
    switch (topology) {
        case RHI::PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case RHI::PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case RHI::PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case RHI::PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ
// ć źĺźĺ¤§ĺ°
// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ

uint32_t GetFormatSize(RHI::Format format) {
    switch (format) {
        case RHI::Format::R8_UNorm:
        case RHI::Format::R8_SNorm:
        case RHI::Format::R8_UInt:
        case RHI::Format::R8_SInt:    return 1;
        case RHI::Format::R16_UNorm:
        case RHI::Format::R16_SNorm:
        case RHI::Format::R16_UInt:
        case RHI::Format::R16_SInt:
        case RHI::Format::R16_Float:
        case RHI::Format::RG8_UNorm:  return 2;
        case RHI::Format::R32_UInt:
        case RHI::Format::R32_SInt:
        case RHI::Format::R32_Float:
        case RHI::Format::RGBA8_UNorm:
        case RHI::Format::RGBA8_sRGB:
        case RHI::Format::BGRA8_UNorm: return 4;
        case RHI::Format::RGBA16_Float: return 8;
        case RHI::Format::RGBA32_Float: return 16;
        case RHI::Format::D32_Float:    return 4;
        case RHI::Format::D24_UNorm_S8_UInt: return 4;
        case RHI::Format::D32_Float_S8_UInt: return 5;
        default: return 0;
    }
}

// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ
// ĺŽäž/čŽžĺ¤ćŠĺą
// ââââââââââââââââââââââââââââââââââââââââââââââââââââââââââââ

std::vector<const char*> GetRequiredInstanceExtensions(bool enableValidation) {
    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_KHR_WIN32_SURFACE_EXTENSION_NAME
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };
    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

std::vector<const char*> GetRequiredDeviceExtensions() {
    return {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };
}

} // namespace Engine