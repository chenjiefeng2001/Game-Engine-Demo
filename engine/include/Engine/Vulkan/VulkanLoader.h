#pragma once

/**
 * @file VulkanLoader.h
 * @brief Volk 元加载器封装 — 延迟加载 Vulkan 函数指针
 *
 * 设计要点：
 *   - 使用 Volk 在运行时加载 Vulkan 函数（不链接 vulkan-1.lib）
 *   - 支持优雅降级：若 Vulkan 不可用，返回 false
 *   - 自动加载实例扩展和设备扩展函数
 */

#include "Engine/Vulkan/VulkanCommon.h"

namespace Engine {
namespace RHI {

/**
 * @brief Volk 加载器单例
 *
 * 生命周期：
 *   1. VulkanLoader::Initialize() — 一次性全局初始化
 *   2. 访问实例/设备函数指针
 *   3. VulkanLoader::Shutdown() — 清理
 */
class VulkanLoader {
public:
    /// 初始化 Volk（加载 vulkan-1.dll 所有导出函数）
    static bool Initialize() noexcept;

    /// 加载实例函数（创建 VkInstance 后调用）
    static bool LoadInstance(VkInstance instance) noexcept;

    /// 加载设备函数（创建 VkDevice 后调用）
    static bool LoadDevice(VkDevice device) noexcept;

    /// 卸载
    static void Shutdown() noexcept;

    /// 是否已初始化
    static bool IsInitialized() noexcept;

private:
    static bool s_Initialized;
};

} // namespace RHI
} // namespace Engine