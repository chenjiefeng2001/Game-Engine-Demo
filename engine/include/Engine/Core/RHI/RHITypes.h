#pragma once

/**
 * @file RHITypes.h
 * @brief RHI 基础类型定义 — 渲染格式、队列类型、资源类型、命令列表类型
 *
 * 所有 RHI 抽象层的枚举和基础结构体均定义在此文件中。
 * 不依赖任何具体 API 头文件。
 */

#include "Engine/Types.h"
#include <cstdint>

namespace Engine {
namespace RHI {

    // ============================================================
    // 像素格式
    // ============================================================
    enum class Format : uint8 {
        Unknown = 0,

        // 8-bit
        R8_UNorm, R8_SNorm, R8_UInt, R8_SInt,

        // 16-bit
        R16_UNorm, R16_SNorm, R16_UInt, R16_SInt, R16_Float,
        RG8_UNorm, RG8_SNorm, RG8_UInt, RG8_SInt,

        // 32-bit
        R32_UInt, R32_SInt, R32_Float,
        RG16_UNorm, RG16_SNorm, RG16_UInt, RG16_SInt, RG16_Float,
        RGBA8_UNorm, RGBA8_SNorm, RGBA8_UInt, RGBA8_SInt, RGBA8_sRGB,
        BGRA8_UNorm, BGRA8_sRGB,

        // 64-bit
        RG32_UInt, RG32_SInt, RG32_Float,
        RGBA16_UNorm, RGBA16_SNorm, RGBA16_UInt, RGBA16_SInt, RGBA16_Float,

        // 128-bit
        RGBA32_UInt, RGBA32_SInt, RGBA32_Float,

        // 深度 / 模板
        D16_UNorm, D24_UNorm_S8_UInt, D32_Float, D32_Float_S8_UInt,

        // BCn 压缩格式
        BC1_UNorm, BC1_sRGB, BC2_UNorm, BC2_sRGB,
        BC3_UNorm, BC3_sRGB, BC4_UNorm, BC5_UNorm,
        BC6H_UFloat, BC6H_SFloat, BC7_UNorm, BC7_sRGB,

        COUNT
    };

    inline const char* FormatName(Format fmt) noexcept {
        switch (fmt) {
            case Format::RGBA8_UNorm: return "RGBA8_UNorm";
            case Format::RGBA8_sRGB:  return "RGBA8_sRGB";
            case Format::BGRA8_UNorm: return "BGRA8_UNorm";
            case Format::D32_Float:   return "D32_Float";
            case Format::D24_UNorm_S8_UInt: return "D24_S8";
            default: return "Other";
        }
    }

    inline uint32 FormatSize(Format fmt) noexcept {
        switch (fmt) {
            case Format::R8_UNorm:         return 1;
            case Format::RG8_UNorm:        return 2;
            case Format::RGBA8_UNorm:      return 4;
            case Format::R32_Float:        return 4;
            case Format::RGBA16_Float:     return 8;
            case Format::RGBA32_Float:     return 16;
            case Format::D32_Float:        return 4;
            case Format::D24_UNorm_S8_UInt: return 4;
            default: return 0;
        }
    }

    // ============================================================
    // 队列类型
    // ============================================================
    enum class QueueType : uint8 {
        Graphics   = 0,   ///< 主渲染队列（支持绘制 + 计算 + 传输）
        Compute    = 1,   ///< 异步计算队列
        Transfer   = 2,   ///< 数据传输（DMA）队列
        COUNT
    };

    // ============================================================
    // 命令列表类型（决定提交到哪个队列）
    // ============================================================
    enum class CommandListType : uint8 {
        Direct     = 0,   ///< 直接命令列表（可提交到任意队列）
        Bundle     = 1,   ///< 可重用的间接命令列表（只录制一次，复用多次）
        Compute    = 2,   ///< 计算专用
        Transfer   = 3,   ///< 传输专用
    };

    // ============================================================
    // 资源状态（用于 Barrier 推导）
    // ============================================================
    enum class ResourceState : uint16 {
        Undefined          = 0,
        VertexBuffer       = 1 << 0,
        IndexBuffer        = 1 << 1,
        ConstantBuffer     = 1 << 2,
        ShaderResource     = 1 << 3,
        UnorderedAccess    = 1 << 4,
        RenderTarget       = 1 << 5,
        DepthStencil       = 1 << 6,
        CopySource         = 1 << 7,
        CopyDest           = 1 << 8,
        Present            = 1 << 9,
        Common             = VertexBuffer | IndexBuffer | ConstantBuffer |
                             ShaderResource | CopySource | CopyDest,
    };

    inline ResourceState operator|(ResourceState a, ResourceState b) noexcept {
        return static_cast<ResourceState>(
            static_cast<uint16>(a) | static_cast<uint16>(b));
    }

    // ============================================================
    // 图元拓扑
    // ============================================================
    enum class PrimitiveTopology : uint8 {
        TriangleList,
        TriangleStrip,
        LineList,
        LineStrip,
        PointList,
    };

    // ============================================================
    // 比较函数
    // ============================================================
    enum class CompareFunc : uint8 {
        Never, Less, Equal, LessEqual, Greater,
        NotEqual, GreaterEqual, Always,
    };

    // ============================================================
    // 混合模式
    // ============================================================
    enum class BlendFactor : uint8 {
        Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha,
        DstAlpha, InvDstAlpha, DstColor, InvDstColor,
    };

    enum class BlendOp : uint8 {
        Add, Subtract, RevSubtract, Min, Max,
    };

    // ============================================================
    // 光栅化器状态
    // ============================================================
    enum class CullMode : uint8 { None, Front, Back };

    enum class FillMode : uint8 { Solid, Wireframe };

    // ============================================================
    // 模板操作
    // ============================================================
    enum class StencilOp : uint8 {
        Keep, Zero, Replace, IncrSat, DecrSat, Invert, Incr, Decr,
    };

} // namespace RHI
} // namespace Engine