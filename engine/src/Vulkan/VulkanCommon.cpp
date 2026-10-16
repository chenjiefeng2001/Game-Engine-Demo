/**
 * @file VulkanCommon.cpp
 * @brief Vulkan ĺąäşŤçąťĺć ĺ°ä¸čžĺŠĺ˝ć°ĺŽç?
 */

#include "Engine/Vulkan/VulkanCommon.h"

#if !defined(_WIN32)
// 平台 surface 扩展头。Vulkan-Headers 的这些头**不**自带 X11 / xcb / Wayland 的
// 前置声明，直接包含会因类型未声明而编译失败；而引入 libX11 / libxcb /
// wayland-client 开发包又需要改 engine/CMakeLists.txt（属他人所有）。
//
// 折中且安全的做法：这些类型在 Vulkan 侧**只以指针或 32 位整数出现**，从不解引用，
// 因此本地声明为不完整类型 / 基本整型即可，无需真正的 SDK 头。
// 下面这组声明与 xcb.h / Xlib.h / wayland-client 的定义相容（重复的同型 typedef
// 在 C++ 中合法）。
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_window_t;
typedef uint32_t xcb_visualid_t;

typedef struct _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long VisualID;

struct wl_display;
struct wl_surface;

#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_xlib.h>
#include <vulkan/vulkan_wayland.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
case Format::RG16_UNorm:   return VK_FORMAT_R16G16_UNORM;
case Format::RG16_SNorm:   return VK_FORMAT_R16G16_SNORM;
case Format::RG16_UInt:    return VK_FORMAT_R16G16_UINT;
case Format::RG16_SInt:    return VK_FORMAT_R16G16_SINT;
case Format::RG16_Float:   return VK_FORMAT_R16G16_SFLOAT;
case Format::RG8_SNorm:    return VK_FORMAT_R8G8_SNORM;
case Format::RG8_UInt:     return VK_FORMAT_R8G8_UINT;
case Format::RG8_SInt:     return VK_FORMAT_R8G8_SINT;
case Format::RG32_UInt:    return VK_FORMAT_R32G32_UINT;
case Format::RG32_SInt:    return VK_FORMAT_R32G32_SINT;
          // RG32_Float 是顶点位置/颜色最常用的格式，此前漏掉后落到 default，
          // 返回 VK_FORMAT_UNDEFINED(0) —— 而 0 是「非法值」而非「未知格式」，
          // 于是 CreateGraphicsPSO 建出了非法管线，绘制时驱动行为不可预期。
case Format::RG32_Float:   return VK_FORMAT_R32G32_SFLOAT;
case Format::RGB32_Float:   return VK_FORMAT_R32G32B32_SFLOAT;
case Format::RGB32_SInt:    return VK_FORMAT_R32G32B32_SINT;
case Format::RGB32_UInt:    return VK_FORMAT_R32G32B32_UINT;
case Format::RGBA8_SNorm:  return VK_FORMAT_R8G8B8A8_SNORM;
case Format::RGBA8_UInt:   return VK_FORMAT_R8G8B8A8_UINT;
case Format::RGBA8_SInt:   return VK_FORMAT_R8G8B8A8_SINT;
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

VkDescriptorType DescriptorKindToVk(RHI::DescriptorKind kind) noexcept {
    switch (kind) {
        case RHI::DescriptorKind::UniformBuffer:          return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case RHI::DescriptorKind::StorageBuffer:          return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case RHI::DescriptorKind::SampledTexture:         return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case RHI::DescriptorKind::Sampler:                return VK_DESCRIPTOR_TYPE_SAMPLER;
        case RHI::DescriptorKind::CombinedImageSampler:   return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

VkCompareOp CompareFuncToVk(RHI::CompareFunc f) noexcept {
    switch (f) {
      case RHI::CompareFunc::Never:        return VK_COMPARE_OP_NEVER;
      case RHI::CompareFunc::Less:         return VK_COMPARE_OP_LESS;
      case RHI::CompareFunc::Equal:        return VK_COMPARE_OP_EQUAL;
      case RHI::CompareFunc::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
      case RHI::CompareFunc::Greater:      return VK_COMPARE_OP_GREATER;
      case RHI::CompareFunc::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
      case RHI::CompareFunc::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
      case RHI::CompareFunc::Always:       return VK_COMPARE_OP_ALWAYS;
      default:                             return VK_COMPARE_OP_LESS_OR_EQUAL;
    }
}
bool PrepareColorAttachment(VkCommandBuffer cmd,
                                VkImageView view,
                                VkImage image,
                                VkImageLayout& outLayout,
                                VkAttachmentLoadOp loadOp,
                                const float clearColor[4],
                                VkRenderingAttachmentInfo& outAttachment) noexcept {
    outAttachment = {};
    outAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    outAttachment.imageView = view;
    // 动态渲染只接受 COLOR_ATTACHMENT_OPTIMAL / GENERAL / SHARED_PRESENT_KHR。
    // 曾经写成 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL，那不是动态渲染的合法附件布局。
    outAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    outAttachment.loadOp = loadOp;
    outAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // loadOp=CLEAR 读的就是 clearValue；漏填会清成零值而不是调用方要的清除色。
    for (uint32 ch = 0; ch < 4; ++ch) outAttachment.clearValue.color.float32[ch] = clearColor[ch];

    // 声明布局不等于转换布局：图像创建后处于 UNDEFINED。
    if (outLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) return false;
    VkImageMemoryBarrier b{};
    b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.image            = image;
    b.oldLayout        = outLayout;
    b.newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask    = outLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0
                             : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    outLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return true;
}

bool IsDepthFormat(RHI::Format format) noexcept {
    switch (format) {
        case RHI::Format::D16_UNorm:
        case RHI::Format::D24_UNorm_S8_UInt:
        case RHI::Format::D32_Float:
        case RHI::Format::D32_Float_S8_UInt: return true;
        default: return false;
    }
}
bool IsStencilFormat(RHI::Format format) noexcept {
    return format == RHI::Format::D24_UNorm_S8_UInt ||
           format == RHI::Format::D32_Float_S8_UInt;
}
VkImageAspectFlags FormatToAspect(RHI::Format format) noexcept {
    const bool d = IsDepthFormat(format);
    const bool s = IsStencilFormat(format);
    if (d && s) return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if (d)      return VK_IMAGE_ASPECT_DEPTH_BIT;
    if (s)      return VK_IMAGE_ASPECT_STENCIL_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

RHI::Format VkToFormat(VkFormat fmt) {
    switch (fmt) {
        case VK_FORMAT_R8_UNORM:         return Format::R8_UNorm;
        case VK_FORMAT_R8G8_UNORM:       return Format::RG8_UNorm;
        case VK_FORMAT_R8G8B8A8_UNORM:   return Format::RGBA8_UNorm;
        case VK_FORMAT_B8G8R8A8_UNORM:   return Format::BGRA8_UNorm;
        case VK_FORMAT_R32_SFLOAT:       return Format::R32_Float;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::RGBA32_Float;
case VK_FORMAT_R32G32B32_SFLOAT:   return Format::RGB32_Float;
case VK_FORMAT_R32G32B32_SINT:     return Format::RGB32_SInt;
case VK_FORMAT_R32G32B32_UINT:     return Format::RGB32_UInt;
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

#if !defined(_WIN32)

namespace {

bool InstanceExtensionPresent(VkInstance instance, const char* name) noexcept {
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkExtensionProperties> exts(count);
    if (count == 0) return false;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data()) != VK_SUCCESS) return false;
    for (uint32 i = 0; i < count; ++i) {
        if (std::strcmp(exts[i].extensionName, name) == 0) return true;
    }
    (void)instance;
    return false;
}

// XCB 优先于 Xlib：GLFW 在 X11 下同时给出两种句柄，XCB 路径不需要额外链接 libX11。
// 但「优先」只在 Auto 时生效；调用方手里是哪种句柄只有它自己知道，必须显式指定，
// 否则会把 Xlib 的 Display* 当成 xcb 连接 reinterpret_cast，类型不匹配且不立即报错。
struct PlatformEntry {
    NativeWindowPlatform platform;
    const char* extension;
};
constexpr PlatformEntry kPlatformSurfaceExtensions[] = {
    { NativeWindowPlatform::Xcb,     VK_KHR_XCB_SURFACE_EXTENSION_NAME     },
    { NativeWindowPlatform::Xlib,    VK_KHR_XLIB_SURFACE_EXTENSION_NAME    },
    { NativeWindowPlatform::Wayland, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME },
};

} // namespace

const char* GetAvailablePlatformSurfaceExtension(VkInstance instance,
                                                NativeWindowPlatform requested) noexcept {
    for (const auto& entry : kPlatformSurfaceExtensions) {
        if (requested != NativeWindowPlatform::Auto && requested != entry.platform) continue;
        if (InstanceExtensionPresent(instance, entry.extension)) return entry.extension;
    }
    return nullptr;
}

bool CreatePlatformSurface(VkInstance instance, const NativeWindow& win,
                           VkSurfaceKHR* outSurface) noexcept {
    if (!outSurface) return false;
    *outSurface = VK_NULL_HANDLE;
    if (!win.window || !win.display) return false;

    // 用与实例创建时相同的规则解析平台，保证两边一致：实例没启用某扩展却去创建
    // 对应的 surface，行为未定义。
    const char* ext = GetAvailablePlatformSurfaceExtension(instance, win.platform);
    if (!ext) {
        std::fprintf(stderr, "[Vulkan] no usable surface extension for the requested platform\n");
        return false;
    }

    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (std::strcmp(ext, VK_KHR_XLIB_SURFACE_EXTENSION_NAME) == 0) {
        VkXlibSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
        sci.dpy    = reinterpret_cast<Display*>(win.display);
        sci.window = reinterpret_cast<::Window>(win.window);
        result = vkCreateXlibSurfaceKHR(instance, &sci, nullptr, outSurface);
    } else if (std::strcmp(ext, VK_KHR_XCB_SURFACE_EXTENSION_NAME) == 0) {
        VkXcbSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
        sci.connection = reinterpret_cast<xcb_connection_t*>(win.display);
        sci.window     = reinterpret_cast<::Window>(win.window);
        result = vkCreateXcbSurfaceKHR(instance, &sci, nullptr, outSurface);
    } else if (std::strcmp(ext, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME) == 0) {
        VkWaylandSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
        sci.display = reinterpret_cast<wl_display*>(win.display);
        sci.surface = reinterpret_cast<wl_surface*>(win.window);
        result = vkCreateWaylandSurfaceKHR(instance, &sci, nullptr, outSurface);
    }

    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] CreatePlatformSurface(%s) failed: %d\n", ext, result);
        *outSurface = VK_NULL_HANDLE;
        return false;
    }
    std::fprintf(stderr, "[Vulkan] surface created via %s\n", ext);
    return true;
}

#endif // !_WIN32

} // namespace Engine
