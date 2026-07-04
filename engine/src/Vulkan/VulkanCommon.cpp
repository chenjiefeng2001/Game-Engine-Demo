/**
 * @file VulkanCommon.cpp
 * @brief Vulkan 共享类型映射与辅助函数实现
 */

#include "Engine/Vulkan/VulkanCommon.h"

#ifndef ENGINE_HAS_VULKAN
#define ENGINE_HAS_VULKAN 1
#endif

#include <cstdio>
#include <cstdlib>

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// Format → VkFormat
// ════════════════════════════════════════════════════════════

VkFormat FormatToVk(Format fmt) noexcept {
    switch (fmt) {
        // 8-bit
        case Format::R8_UNorm:     return VK_FORMAT_R8_UNORM;
        case Format::R8_SNorm:     return VK_FORMAT_R8_SNORM;
        case Format::R8_UInt:      return VK_FORMAT_R8_UINT;
        case Format::R8_SInt:      return VK_FORMAT_R8_SINT;

        // 16-bit
        case Format::R16_UNorm:    return VK_FORMAT_R16_UNORM;
        case Format::R16_SNorm:    return VK_FORMAT_R16_SNORM;
        case Format::R16_UInt:     return VK_FORMAT_R16_UINT;
        case Format::R16_SInt:     return VK_FORMAT_R16_SINT;
        case Format::R16_Float:    return VK_FORMAT_R16_SFLOAT;
        case Format::RG8_UNorm:    return VK_FORMAT_R8G8_UNORM;
        case Format::RG8_SNorm:    return VK_FORMAT_R8G8_SNORM;
        case Format::RG8_UInt:     return VK_FORMAT_R8G8_UINT;
        case Format::RG8_SInt:     return VK_FORMAT_R8G8_SINT;

        // 32-bit
        case Format::R32_UInt:     return VK_FORMAT_R32_UINT;
        case Format::R32_SInt:     return VK_FORMAT_R32_SINT;
        case Format::R32_Float:    return VK_FORMAT_R32_SFLOAT;
        case Format::RG16_UNorm:   return VK_FORMAT_R16G16_UNORM;
        case Format::RG16_SNorm:   return VK_FORMAT_R16G16_SNORM;
        case Format::RG16_UInt:    return VK_FORMAT_R16G16_UINT;
        case Format::RG16_SInt:    return VK_FORMAT_R16G16_SINT;
        case Format::RG16_Float:   return VK_FORMAT_R16G16_SFLOAT;
        case Format::RGBA8_UNorm:  return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SNorm:  return VK_FORMAT_R8G8B8A8_SNORM;
        case Format::RGBA8_UInt:   return VK_FORMAT_R8G8B8A8_UINT;
        case Format::RGBA8_SInt:   return VK_FORMAT_R8G8B8A8_SINT;
        case Format::RGBA8_sRGB:   return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::BGRA8_UNorm:  return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRA8_sRGB:   return VK_FORMAT_B8G8R8A8_SRGB;

        // 64-bit
        case Format::RG32_UInt:    return VK_FORMAT_R32G32_UINT;
        case Format::RG32_SInt:    return VK_FORMAT_R32G32_SINT;
        case Format::RG32_Float:   return VK_FORMAT_R32G32_SFLOAT;
        case Format::RGBA16_UNorm: return VK_FORMAT_R16G16B16A16_UNORM;
        case Format::RGBA16_SNorm: return VK_FORMAT_R16G16B16A16_SNORM;
        case Format::RGBA16_UInt:  return VK_FORMAT_R16G16B16A16_UINT;
        case Format::RGBA16_SInt:  return VK_FORMAT_R16G16B16A16_SINT;
        case Format::RGBA16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;

        // 128-bit
        case Format::RGBA32_UInt:  return VK_FORMAT_R32G32B32A32_UINT;
        case Format::RGBA32_SInt:  return VK_FORMAT_R32G32B32A32_SINT;
        case Format::RGBA32_Float: return VK_FORMAT_R32G32B32A32_SFLOAT;

        // 深度 / 模板
        case Format::D16_UNorm:          return VK_FORMAT_D16_UNORM;
        case Format::D24_UNorm_S8_UInt:  return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32_Float:          return VK_FORMAT_D32_SFLOAT;
        case Format::D32_Float_S8_UInt:  return VK_FORMAT_D32_SFLOAT_S8_UINT;

        // BCn 压缩
        case Format::BC1_UNorm:   return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case Format::BC1_sRGB:    return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case Format::BC2_UNorm:   return VK_FORMAT_BC2_UNORM_BLOCK;
        case Format::BC2_sRGB:    return VK_FORMAT_BC2_SRGB_BLOCK;
        case Format::BC3_UNorm:   return VK_FORMAT_BC3_UNORM_BLOCK;
        case Format::BC3_sRGB:    return VK_FORMAT_BC3_SRGB_BLOCK;
        case Format::BC4_UNorm:   return VK_FORMAT_BC4_UNORM_BLOCK;
        case Format::BC5_UNorm:   return VK_FORMAT_BC5_UNORM_BLOCK;
        case Format::BC6H_UFloat: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case Format::BC6H_SFloat: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case Format::BC7_UNorm:   return VK_FORMAT_BC7_UNORM_BLOCK;
        case Format::BC7_sRGB:    return VK_FORMAT_BC7_SRGB_BLOCK;

        default:
            return VK_FORMAT_UNDEFINED;
    }
}

// ════════════════════════════════════════════════════════════
// VkFormat → Format
// ════════════════════════════════════════════════════════════

Format VkFormatToEngine(VkFormat fmt) noexcept {
    switch (fmt) {
        case VK_FORMAT_R8_UNORM:              return Format::R8_UNorm;
        case VK_FORMAT_R8_SNORM:              return Format::R8_SNorm;
        case VK_FORMAT_R8_UINT:               return Format::R8_UInt;
        case VK_FORMAT_R8_SINT:               return Format::R8_SInt;
        case VK_FORMAT_R16_UNORM:             return Format::R16_UNorm;
        case VK_FORMAT_R16_SNORM:             return Format::R16_SNorm;
        case VK_FORMAT_R16_UINT:              return Format::R16_UInt;
        case VK_FORMAT_R16_SINT:              return Format::R16_SInt;
        case VK_FORMAT_R16_SFLOAT:            return Format::R16_Float;
        case VK_FORMAT_R8G8_UNORM:            return Format::RG8_UNorm;
        case VK_FORMAT_R8G8_SNORM:            return Format::RG8_SNorm;
        case VK_FORMAT_R8G8_UINT:             return Format::RG8_UInt;
        case VK_FORMAT_R8G8_SINT:             return Format::RG8_SInt;
        case VK_FORMAT_R32_UINT:              return Format::R32_UInt;
        case VK_FORMAT_R32_SINT:              return Format::R32_SInt;
        case VK_FORMAT_R32_SFLOAT:            return Format::R32_Float;
        case VK_FORMAT_R16G16_UNORM:          return Format::RG16_UNorm;
        case VK_FORMAT_R16G16_SNORM:          return Format::RG16_SNorm;
        case VK_FORMAT_R16G16_UINT:           return Format::RG16_UInt;
        case VK_FORMAT_R16G16_SINT:           return Format::RG16_SInt;
        case VK_FORMAT_R16G16_SFLOAT:         return Format::RG16_Float;
        case VK_FORMAT_R8G8B8A8_UNORM:        return Format::RGBA8_UNorm;
        case VK_FORMAT_R8G8B8A8_SNORM:        return Format::RGBA8_SNorm;
        case VK_FORMAT_R8G8B8A8_UINT:         return Format::RGBA8_UInt;
        case VK_FORMAT_R8G8B8A8_SINT:         return Format::RGBA8_SInt;
        case VK_FORMAT_R8G8B8A8_SRGB:         return Format::RGBA8_sRGB;
        case VK_FORMAT_B8G8R8A8_UNORM:        return Format::BGRA8_UNorm;
        case VK_FORMAT_B8G8R8A8_SRGB:         return Format::BGRA8_sRGB;
        case VK_FORMAT_R32G32_UINT:           return Format::RG32_UInt;
        case VK_FORMAT_R32G32_SINT:           return Format::RG32_SInt;
        case VK_FORMAT_R32G32_SFLOAT:         return Format::RG32_Float;
        case VK_FORMAT_R16G16B16A16_UNORM:    return Format::RGBA16_UNorm;
        case VK_FORMAT_R16G16B16A16_SNORM:    return Format::RGBA16_SNorm;
        case VK_FORMAT_R16G16B16A16_UINT:     return Format::RGBA16_UInt;
        case VK_FORMAT_R16G16B16A16_SINT:     return Format::RGBA16_SInt;
        case VK_FORMAT_R16G16B16A16_SFLOAT:   return Format::RGBA16_Float;
        case VK_FORMAT_R32G32B32A32_UINT:     return Format::RGBA32_UInt;
        case VK_FORMAT_R32G32B32A32_SINT:     return Format::RGBA32_SInt;
        case VK_FORMAT_R32G32B32A32_SFLOAT:   return Format::RGBA32_Float;
        case VK_FORMAT_D16_UNORM:             return Format::D16_UNorm;
        case VK_FORMAT_D24_UNORM_S8_UINT:     return Format::D24_UNorm_S8_UInt;
        case VK_FORMAT_D32_SFLOAT:            return Format::D32_Float;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:    return Format::D32_Float_S8_UInt;
        default:
            return Format::Unknown;
    }
}

// ════════════════════════════════════════════════════════════
// 资源状态 → VkImageLayout
// ════════════════════════════════════════════════════════════

VkImageLayout ResourceStateToLayout(ResourceState state) noexcept {
    using RS = ResourceState;
    switch (state) {
        case RS::Undefined:      return VK_IMAGE_LAYOUT_UNDEFINED;
        case RS::RenderTarget:   return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RS::DepthStencil:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case RS::ShaderResource: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RS::CopySource:     return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case RS::CopyDest:       return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RS::Present:        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        case RS::UnorderedAccess: return VK_IMAGE_LAYOUT_GENERAL;
        default:
            // 对于 Buffer 状态，返回 GENERAL（不涉及布局变化）
            return VK_IMAGE_LAYOUT_GENERAL;
    }
}

// ════════════════════════════════════════════════════════════
// 资源状态 → VkPipelineStageFlags
// ════════════════════════════════════════════════════════════

VkPipelineStageFlags ResourceStateToStage(ResourceState state) noexcept {
    using RS = ResourceState;
    VkPipelineStageFlags flags = 0;

    // 由于 ResourceState 是位掩码，支持组合状态
    auto s = static_cast<uint16_t>(state);

    if (s & static_cast<uint16_t>(RS::RenderTarget))
        flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    if (s & static_cast<uint16_t>(RS::DepthStencil))
        flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
               | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

    if (s & static_cast<uint16_t>(RS::ShaderResource))
        flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
               | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    if (s & static_cast<uint16_t>(RS::CopySource) ||
        s & static_cast<uint16_t>(RS::CopyDest))
        flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (s & static_cast<uint16_t>(RS::UnorderedAccess))
        flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    // 顶点/索引/常量缓冲 在 VERTEX_INPUT 阶段
    if (s & static_cast<uint16_t>(RS::VertexBuffer) ||
        s & static_cast<uint16_t>(RS::IndexBuffer)  ||
        s & static_cast<uint16_t>(RS::ConstantBuffer))
        flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

    if (s & static_cast<uint16_t>(RS::Present))
        flags |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    // 默认 fallback
    if (flags == 0)
        flags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    return flags;
}

// ════════════════════════════════════════════════════════════
// 资源状态 → VkAccessFlags
// ════════════════════════════════════════════════════════════

VkAccessFlags ResourceStateToAccess(ResourceState state) noexcept {
    using RS = ResourceState;
    VkAccessFlags flags = 0;
    auto s = static_cast<uint16_t>(state);

    if (s & static_cast<uint16_t>(RS::RenderTarget))
        flags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
               | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    if (s & static_cast<uint16_t>(RS::DepthStencil))
        flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
               | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    if (s & static_cast<uint16_t>(RS::ShaderResource))
        flags |= VK_ACCESS_SHADER_READ_BIT;

    if (s & static_cast<uint16_t>(RS::UnorderedAccess))
        flags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    if (s & static_cast<uint16_t>(RS::CopySource))
        flags |= VK_ACCESS_TRANSFER_READ_BIT;

    if (s & static_cast<uint16_t>(RS::CopyDest))
        flags |= VK_ACCESS_TRANSFER_WRITE_BIT;

    if (s & static_cast<uint16_t>(RS::VertexBuffer))
        flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

    if (s & static_cast<uint16_t>(RS::IndexBuffer))
        flags |= VK_ACCESS_INDEX_READ_BIT;

    if (s & static_cast<uint16_t>(RS::ConstantBuffer))
        flags |= VK_ACCESS_UNIFORM_READ_BIT;

    if (s & static_cast<uint16_t>(RS::Present))
        flags |= 0; // 不涉及访问

    if (flags == 0)
        flags = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    return flags;
}

// ════════════════════════════════════════════════════════════
// 屏障转换
// ════════════════════════════════════════════════════════════

void ConvertBarrierDesc(
    const ResourceBarrierDesc& src,
    VkImageMemoryBarrier& outVkBarrier,
    VkPipelineStageFlags& outSrcStage,
    VkPipelineStageFlags& outDstStage) noexcept
{
    outVkBarrier = {};
    outVkBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    outVkBarrier.oldLayout = ResourceStateToLayout(src.stateBefore);
    outVkBarrier.newLayout = ResourceStateToLayout(src.stateAfter);
    outVkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outVkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outVkBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    outVkBarrier.subresourceRange.baseMipLevel = 0;
    outVkBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    outVkBarrier.subresourceRange.baseArrayLayer = 0;
    outVkBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    // 访问掩码
    outSrcStage = ResourceStateToStage(src.stateBefore);
    outDstStage = ResourceStateToStage(src.stateAfter);
    outVkBarrier.srcAccessMask = ResourceStateToAccess(src.stateBefore);
    outVkBarrier.dstAccessMask = ResourceStateToAccess(src.stateAfter);
}

// ════════════════════════════════════════════════════════════
// VkCheck
// ════════════════════════════════════════════════════════════

bool VkCheck(VkResult result, const char* file, int line, const char* expr) noexcept {
    if (result == VK_SUCCESS) return true;

    const char* msg = "Unknown Vulkan error";
    switch (result) {
        case VK_NOT_READY:             msg = "Not ready"; break;
        case VK_TIMEOUT:               msg = "Timeout"; break;
        case VK_EVENT_SET:             msg = "Event set"; break;
        case VK_EVENT_RESET:           msg = "Event reset"; break;
        case VK_INCOMPLETE:            msg = "Incomplete"; break;
        case VK_ERROR_OUT_OF_HOST_MEMORY:  msg = "Out of host memory"; break;
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: msg = "Out of device memory"; break;
        case VK_ERROR_INITIALIZATION_FAILED: msg = "Initialization failed"; break;
        case VK_ERROR_DEVICE_LOST:     msg = "Device lost"; break;
        case VK_ERROR_MEMORY_MAP_FAILED: msg = "Memory map failed"; break;
        case VK_ERROR_LAYER_NOT_PRESENT: msg = "Layer not present"; break;
        case VK_ERROR_EXTENSION_NOT_PRESENT: msg = "Extension not present"; break;
        case VK_ERROR_FEATURE_NOT_PRESENT: msg = "Feature not present"; break;
        case VK_ERROR_INCOMPATIBLE_DRIVER: msg = "Incompatible driver"; break;
        case VK_ERROR_TOO_MANY_OBJECTS: msg = "Too many objects"; break;
        case VK_ERROR_FORMAT_NOT_SUPPORTED: msg = "Format not supported"; break;
        case VK_ERROR_FRAGMENTED_POOL: msg = "Fragmented pool"; break;
        case VK_ERROR_UNKNOWN:         msg = "Unknown error"; break;
        case VK_ERROR_OUT_OF_POOL_MEMORY: msg = "Out of pool memory"; break;
        case VK_ERROR_INVALID_SHADER_NV: msg = "Invalid shader"; break;
#ifdef VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: msg = "Invalid DRM format"; break;
#endif
#ifdef VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: msg = "Full screen exclusive lost"; break;
#endif
        default: break;
    }

    // 使用 snprintf 在栈上格式化错误日志
    char buf[512];
    snprintf(buf, sizeof(buf),
        "[Vulkan] %s:%d: %s = %s (0x%x)",
        file, line, expr, msg, static_cast<int>(result));
    // 写日志：这里使用 spdlog 或直接输出
    // 由于在 RHI 底层，先使用 stderr 输出
    std::fprintf(stderr, "%s\n", buf);

    // Debug 模式下触发 DebugBreak
#ifdef _DEBUG
    __debugbreak();
#endif

    return false;
}

// ════════════════════════════════════════════════════════════
// 辅助函数
// ════════════════════════════════════════════════════════════

std::string GetVulkanAppName() noexcept {
    return "GameEngineDemo";
}

const char* QueueTypeName(QueueType type) noexcept {
    switch (type) {
        case QueueType::Graphics: return "Graphics";
        case QueueType::Compute:  return "Compute";
        case QueueType::Transfer: return "Transfer";
        default: return "Unknown";
    }
}

std::vector<const char*> GetRequiredInstanceExtensions(bool enableValidation) noexcept {
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

std::vector<const char*> GetRequiredDeviceExtensions() noexcept {
    return {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };
}

} // namespace RHI
} // namespace Engine