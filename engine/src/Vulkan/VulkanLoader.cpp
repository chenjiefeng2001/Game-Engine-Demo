/**
 * @file VulkanLoader.cpp
 * @brief Volk 元加载器封装实现
 */

#include "Engine/Vulkan/VulkanLoader.h"

// Volk 头文件（需要在所有 Vulkan 头文件前包含）
#include <volk/volk.h>

namespace Engine {
namespace RHI {

bool VulkanLoader::s_Initialized = false;

bool VulkanLoader::Initialize() noexcept {
    if (s_Initialized) return true;

    VkResult result = volkInitialize();
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[VulkanLoader] volkInitialize failed: %d\n",
                     static_cast<int>(result));
        return false;
    }

    s_Initialized = true;
    return true;
}

bool VulkanLoader::LoadInstance(VkInstance instance) noexcept {
    if (!s_Initialized || instance == VK_NULL_HANDLE) return false;
    volkLoadInstance(instance);
    return true;
}

bool VulkanLoader::LoadDevice(VkDevice device) noexcept {
    if (!s_Initialized || device == VK_NULL_HANDLE) return false;
    volkLoadDevice(device);
    return true;
}

void VulkanLoader::Shutdown() noexcept {
    s_Initialized = false;
}

bool VulkanLoader::IsInitialized() noexcept {
    return s_Initialized;
}

} // namespace RHI
} // namespace Engine