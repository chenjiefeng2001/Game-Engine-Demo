#pragma once

/**
 * @file VulkanCommon.h
 * @brief Vulkan 后端共享类型、枚举映射、辅助函数
 *
 * 包含：
 *   - Vulkan 版本/扩展/层所需的常量和宏
 *   - ResourceState → VkImageLayout 映射
 *   - Format → VkFormat 映射
 *   - 其他跨 Vulkan 实现的工具函数
 */

#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RHI/GPUAllocation.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/IRHIDevice.h"

// Windows 平台宏（必须早于任何 Vulkan 头文件）
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR

// Vulkan SDK 直接提供函数原型和类型定义
#include <vulkan/vulkan.h>

// VMA 使用静态 Vulkan 函数
#include <vk_mem_alloc.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <mutex>
#include <unordered_map>
#include <thread>
#include <memory>

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// 常量和配置
// ════════════════════════════════════════════════════════════

/// Vulkan 后端最低要求版本：1.3
constexpr uint32_t kRequiredVulkanVersion = VK_API_VERSION_1_3;

/// 最大飞行帧数
constexpr uint32_t kMaxFramesInFlight = 3;

/// 动态 Uniform Buffer 大小（每帧 16MB，三帧 = 48MB）
constexpr uint32_t kDynamicUBOSizePerFrame = 16 * 1024 * 1024;

/// 描述符池最大资源数
constexpr uint32_t kMaxDescriptorCount = 16384;

/// 每个工作线程的默认命令池重置标志
constexpr VkCommandPoolCreateFlags kThreadCmdPoolFlags =
    VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

// ════════════════════════════════════════════════════════════
// 格式映射
// ════════════════════════════════════════════════════════════

/// Format → VkFormat
VkFormat FormatToVk(Format fmt) noexcept;

/// VkFormat → Format
Format VkFormatToEngine(VkFormat fmt) noexcept;

// ════════════════════════════════════════════════════════════
// 资源状态 → VkImageLayout 映射
// ════════════════════════════════════════════════════════════

/// ResourceState → VkImageLayout
VkImageLayout ResourceStateToLayout(ResourceState state) noexcept;

/// ResourceState → VkPipelineStageFlags (自动阶段推导)
VkPipelineStageFlags ResourceStateToStage(ResourceState state) noexcept;

/// ResourceState → VkAccessFlags
VkAccessFlags ResourceStateToAccess(ResourceState state) noexcept;

// ════════════════════════════════════════════════════════════
// 屏障辅助
// ════════════════════════════════════════════════════════════

/// 将 ResourceBarrierDesc 转换为 VkImageMemoryBarrier
void ConvertBarrierDesc(
    const ResourceBarrierDesc& src,
    VkImageMemoryBarrier& outVkBarrier,
    VkPipelineStageFlags& outSrcStage,
    VkPipelineStageFlags& outDstStage) noexcept;

// ════════════════════════════════════════════════════════════
// 队列类型映射
// ════════════════════════════════════════════════════════════

/// QueueType → 队列族类型字符串
const char* QueueTypeName(QueueType type) noexcept;

// ════════════════════════════════════════════════════════════
// 其他 Vulkan 辅助
// ════════════════════════════════════════════════════════════

/// 检查 VkResult 并记录错误
bool VkCheck(VkResult result, const char* file, int line, const char* expr) noexcept;

#define VK_CHECK(expr) VkCheck((expr), __FILE__, __LINE__, #expr)

/// 构建 Vulkan 应用程序名称
std::string GetVulkanAppName() noexcept;

/// 获取所需扩展列表
std::vector<const char*> GetRequiredInstanceExtensions(bool enableValidation) noexcept;

/// 获取所需设备扩展列表
std::vector<const char*> GetRequiredDeviceExtensions() noexcept;

} // namespace RHI
} // namespace Engine