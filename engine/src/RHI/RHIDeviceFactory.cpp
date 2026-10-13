/**
 * @file RHIDeviceFactory.cpp
 * @brief 后端 → IRHIDevice 的创建入口实现
 *
 * 顺带补上 RHIBackend.h 中「已声明但从未定义」的 IsBackendAvailable() 与
 * GetDefaultBackend()——此前任何调用者都会得到链接错误。
 *
 * D3D12IRHIDevice.h 包含 d3d12.h，只在 Windows + ENGINE_HAS_D3D12 下可见，
 * 因此该分支必须双重守卫，否则非 Windows 构建会直接编译失败。
 */

#include "Engine/Core/RHI/RHIDeviceFactory.h"

#include "Engine/Core/RHI/GL46AZDODevice.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"

#if defined(ENGINE_HAS_D3D12) && defined(_WIN32)
#include "Engine/Core/RHI/D3D12IRHIDevice.h"
#endif

#include <cstdio>

namespace Engine {
namespace RHI {

bool IsBackendAvailable(Backend b) noexcept {
    switch (b) {
        // GL46Device 的实现始终参与编译；是否有可用 context 是运行期问题，
        // 由调用方通过 InitializeWithGLContext 保证。
        case Backend::OpenGL46:
            return true;
        case Backend::Vulkan:
            return HasVulkanSupport();
        case Backend::D3D12:
#if defined(ENGINE_HAS_D3D12) && defined(_WIN32)
            return HasD3D12Support();
#else
            return false;
#endif
        case Backend::Metal:
        case Backend::Null:
            return false;
        default:
            return false;
    }
}

Backend GetDefaultBackend() noexcept {
    // 策略顺序：优先具备完整运行时验证路径的后端。
    // 注意当前**没有任何生产代码调用本函数**（真正的调用方在 RHIWindow / Application，
    // 属 HRC 所有），因此这个顺序目前只是占位，不改变任何现有行为。
    if (IsBackendAvailable(Backend::Vulkan))   return Backend::Vulkan;
    if (IsBackendAvailable(Backend::D3D12))    return Backend::D3D12;
    return Backend::OpenGL46;
}

std::unique_ptr<IRHIDevice> CreateRHIDevice(Backend backend,
                                            void* windowHandle,
                                            uint32_t width,
                                            uint32_t height) {
    switch (backend) {
        case Backend::OpenGL46: {
            auto dev = std::make_unique<GL46Device>();
            if (!dev->Initialize(windowHandle, width, height)) return nullptr;
            // Initialize 传 nullptr 时 GL46Device 会退化为 stub 并返回 true。
            // 这里明确告知，避免把「假装成功」当成「后端可用」。
            if (!windowHandle) {
                std::fprintf(stderr,
                             "[RHI] OpenGL46 requested without a context: created a stub device, "
                             "not a working renderer\n");
            }
            return dev;
        }

        case Backend::Vulkan: {
            if (!HasVulkanSupport()) {
                std::fprintf(stderr, "[RHI] Vulkan backend not available on this system\n");
                return nullptr;
            }
            auto dev = std::make_unique<VulkanDevice>();
            if (!dev->Initialize(windowHandle, width, height)) return nullptr;
            return dev;
        }

        case Backend::D3D12: {
#if defined(ENGINE_HAS_D3D12) && defined(_WIN32)
            if (!HasD3D12Support()) {
                std::fprintf(stderr, "[RHI] D3D12 backend not available on this system\n");
                return nullptr;
            }
            auto dev = std::make_unique<D3D12Device>();
            if (!dev->Initialize(windowHandle, width, height)) return nullptr;
            return dev;
#else
            std::fprintf(stderr, "[RHI] D3D12 backend is not compiled into this build\n");
            return nullptr;
#endif
        }

        case Backend::Metal:
            std::fprintf(stderr, "[RHI] Metal backend is not implemented\n");
            return nullptr;

        case Backend::Null:
        case Backend::Default:
        default:
            std::fprintf(stderr, "[RHI] CreateRHIDevice: unsupported backend %d\n",
                         static_cast<int>(backend));
            return nullptr;
    }
}

} // namespace RHI
} // namespace Engine