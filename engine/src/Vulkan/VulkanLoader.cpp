/**
 * @file VulkanLoader.cpp
 * @brief Vulkan 函数加载器封装
 */

#include "Engine/Vulkan/VulkanLoader.h"

// 使用 Vulkan SDK 的直接链接，无需 Volk
// vulkan-1.lib 由 CMake 的 Vulkan::Vulkan 目标提供

namespace Engine {
namespace RHI {

bool VulkanLoader::s_Initialized = false;

bool VulkanLoader::Initialize() noexcept {
    // 使用 Vulkan SDK 的原生加载：函数原型由 vulkan.h 提供
    // vulkan-1.lib 中的 vkGetInstanceProcAddr 在运行时动态解析
    s_Initialized = true;
    return true;
}

bool VulkanLoader::LoadInstance(VkInstance) noexcept {
    return s_Initialized;
}

bool VulkanLoader::LoadDevice(VkDevice) noexcept {
    return s_Initialized;
}

void VulkanLoader::Shutdown() noexcept {
    s_Initialized = false;
}

bool VulkanLoader::IsInitialized() noexcept {
    return s_Initialized;
}

} // namespace RHI
} // namespace Engine