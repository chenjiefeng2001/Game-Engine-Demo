/**
 * @file RHIBackend.cpp
 * @brief 多后端 RHI 可用性检测 + 工厂实现
 */

#include "Engine/Core/RHI/RHIBackend.h"
#include "Engine/Core/IGraphicsFactory.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Engine {
namespace RHI {

    bool IsBackendAvailable(Backend b) noexcept {
        switch (b) {
            case Backend::OpenGL46:
                // GLFW/GLAD 集成后始终可用（平台检测在 GLFW 初始化时完成）
                return true;

            case Backend::Vulkan:
#ifdef _WIN32
                // Windows: 检查 Vulkan Loader 是否存在
                {
                    HMODULE hMod = LoadLibraryA("vulkan-1.dll");
                    if (hMod) { FreeLibrary(hMod); return true; }
                }
                return false;
#elif defined(__linux__)
                return true; // Linux: Vulkan Loader 存在于标准系统路径
#else
                return false;
#endif

            case Backend::D3D12:
#ifdef _WIN32
                {
                    HMODULE hMod = LoadLibraryA("d3d12.dll");
                    if (hMod) { FreeLibrary(hMod); return true; }
                }
                return false;
#else
                return false;
#endif

            case Backend::Metal:
#if defined(__APPLE__)
                return true;
#else
                return false;
#endif

            case Backend::Null:
                return true;

            case Backend::Default:
                return GetDefaultBackend() != Backend::Null;

            default:
                return false;
        }
    }

    Backend GetDefaultBackend() noexcept {
#if defined(__APPLE__)
        return Backend::Metal;
#elif defined(_WIN32)
        return Backend::OpenGL46; // D3D12 可在运行时按需切换
#elif defined(__linux__)
        return Backend::Vulkan;
#else
        return Backend::Null;
#endif
    }

} // namespace RHI

    // ============================================================
    // 全局 RHI 工厂函数
    // ============================================================
    std::unique_ptr<IGraphicsFactory> CreateRHI(RHI::Backend backend) {
        // 目前仅返回 nullptr — 实际创建在引擎启动时由 GLFW 后端接管
        // 未来的 Vulkan/D3D12 后端在此处路由到对应的工厂构造函数
        (void)backend;
        return nullptr;
    }

} // namespace Engine