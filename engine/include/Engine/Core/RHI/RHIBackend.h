#pragma once

/**
 * @file RHIBackend.h
 * @brief RHI 后端类型枚举 — 编译时/运行时选择图形 API
 *
 * 支持的后端：
 *   OpenGL 4.6+  — AZDO (DSA + Persistent Mapping + MultiDrawIndirect)
 *   Vulkan        — VMA + Bindless
 *   D3D12         — D3D12MA + Descriptor Heap
 *   Metal         — MTLHeap
 *   Null           — Headless / 测试模式
 */

#include "Engine/Types.h"

namespace Engine {
namespace RHI {

    enum class Backend : uint8 {
        OpenGL46 = 0,   ///< OpenGL 4.6 with AZDO extensions
        Vulkan   = 1,   ///< Vulkan 1.3+ with VMA
        D3D12    = 2,   ///< Direct3D 12 with D3D12MA
        Metal    = 3,   ///< Metal 3.0+ (Apple Silicon)
        Null     = 4,   ///< Headless / testing / no rendering
        Default  = 255  ///< Auto-select best available
    };

    inline const char* BackendName(Backend b) noexcept {
        switch (b) {
            case Backend::OpenGL46: return "OpenGL 4.6 (AZDO)";
            case Backend::Vulkan:   return "Vulkan 1.3";
            case Backend::D3D12:    return "Direct3D 12";
            case Backend::Metal:    return "Metal 3.0";
            case Backend::Null:     return "Null (Headless)";
            case Backend::Default:  return "Auto-select";
            default: return "Unknown";
        }
    }

    /** 查询指定后端在当前平台是否可用 */
    bool IsBackendAvailable(Backend b) noexcept;

    /** 获取当前平台的默认后端 */
    Backend GetDefaultBackend() noexcept;

} // namespace RHI

/** 创建指定后端的图形工厂 — 返回 IGraphicsFactory unique_ptr */
std::unique_ptr<class IGraphicsFactory> CreateRHI(RHI::Backend backend);

} // namespace Engine