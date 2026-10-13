#pragma once

/**
 * @file RHIDeviceFactory.h
 * @brief 后端 → IRHIDevice 的创建入口
 *
 * 背景：此前 VulkanDevice / D3D12Device 都实现了 IRHIDevice，但**没有任何生产代码
 * 创建它们**—— CreateVulkanDevice() 在 engine/src 内的调用者为零，RHIWindow 也不持有
 * 设备。结果是这些后端只能被 sandbox 测试触及，引擎运行时无法选择它们。
 *
 * 本文件提供该缺口缺失的接缝。注意 src/RHI 下的 .cpp 由 CMake 的 file(GLOB ...
 * CONFIGURE_DEPENDS) 收集，因此新增此文件无需改动 engine/CMakeLists.txt。
 */

#include "Engine/Core/RHI/RHIBackend.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include <memory>

namespace Engine {
namespace RHI {

/**
 * 创建并初始化指定后端的 RHI 设备。
 *
 * windowHandle 的语义**因后端而异**，调用方必须自行匹配：
 *   - Vulkan / D3D12：原生窗口句柄（Windows HWND，X11/Wayland 下为对应 native handle）
 *   - OpenGL46      ：GLFWwindow*，且必须已有 current context
 *
 * 传 nullptr 时：Vulkan / D3D12 会做无窗口初始化（适合离屏）；OpenGL46 会退化为
 * **stub 设备并仍然返回 true**——它不会失败，只会假装成功。需要真实 OpenGL 时请
 * 改用 GL46Device::InitializeWithGLContext()。
 *
 * @return 设备指针；后端不可用或初始化失败时返回 nullptr。
 */
std::unique_ptr<IRHIDevice> CreateRHIDevice(Backend backend,
                                            void* windowHandle,
                                            uint32_t width,
                                            uint32_t height);

} // namespace RHI
} // namespace Engine