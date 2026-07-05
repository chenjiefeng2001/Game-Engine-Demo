#pragma once

/**
 * @file LightTypes.h
 * @brief 光照数据类型定义 — 与 GPU 着色器共享的 std140 布局
 *
 * 所有结构体对齐到 16 字节（std140 规范），
 * 可直接 memcpy 到 Uniform Buffer 供着色器使用。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <vector>
#include <cstdint>

namespace Engine {
namespace Rendering {

    // ============================================================
    // 光源类型枚举
    // ============================================================
    enum class LightType : uint32 {
        Directional = 0,
        Point       = 1,
        Spot        = 2,
    };

    // ============================================================
    // 单个光源数据（std140, 64 bytes）
    // ============================================================
    struct alignas(16) GPULight {
        Vec3    position;       // 16: xyz + pad
        float   pad0;
        Vec3    direction;      // 16: xyz + pad
        float   pad1;
        Vec4    color;          // 16: RGB + intensity
        float   range;          // 4: 点光源衰减距离
        float   spotInnerAngle; // 4: 聚光内角 cos
        float   spotOuterAngle; // 4: 聚光外角 cos
        uint32  type;           // 4: LightType (directional/point/spot)
    };
    static_assert(sizeof(GPULight) == 64, "GPULight must be 64 bytes for std140");
    static_assert(alignof(GPULight) == 16, "GPULight must be 16-byte aligned");

    // ============================================================
    // 场景光源列表（CPU 端）
    // ============================================================
    struct SceneLight {
        Vec3    position      = {0, 0, 0};
        Vec3    direction     = {0, -1, 0};
        Vec4    color         = {1, 1, 1, 1};  // RGB + intensity
        float   range         = 100.0f;
        float   spotInnerAngle = 0.0f;  // cos(angle) — 0 = no spot
        float   spotOuterAngle = 0.0f;
        LightType type        = LightType::Directional;
        bool    castsShadow   = true;
    };

    // ============================================================
    // 级联阴影贴图参数（std140, 80 bytes）
    // ============================================================
    struct alignas(16) CascadeParams {
        Mat4    lightViewProj;    // 64 bytes: 光源空间的 VP 矩阵
        float   splitDepth;       // 4: 级联远平面（视图空间 Z）
        float   pad0[3];          // 12: std140 padding
    };
    static_assert(sizeof(CascadeParams) == 80, "CascadeParams must be 80 bytes");

    // ============================================================
    // CSM UBO 数据（std140, 每个级联 80 bytes, 4 个级联 = 320 bytes）
    // ============================================================
    struct alignas(16) CSMUBOData {
        static constexpr uint32 kMaxCascades = 4;

        CascadeParams cascades[kMaxCascades];
        uint32        cascadeCount = 0;
        float         shadowMapSize = 1024.0f;
        float         shadowBias    = 0.005f;
        float         _pad0;
    };
    static_assert(sizeof(CSMUBOData) == 80 * 4 + 16, "CSMUBOData size check");

    // ============================================================
    // 主流 API 光晕/光源计算的常量
    // ============================================================
    namespace LightConstants {
        // Cook-Torrance PBR 常量
        constexpr float PI = 3.14159265358979323846f;

        // 级联阴影默认参数
        constexpr uint32 kDefaultShadowMapSize = 2048;
        constexpr uint32 kDefaultCascadeCount  = 4;
        constexpr float  kDefaultSplitLambda   = 0.95f;  // 对数/均匀混合
        constexpr float  kDefaultShadowBias    = 0.005f;
        constexpr float  kDefaultNearZ         = 0.1f;
        constexpr float  kDefaultFarZ          = 500.0f;
    }

} // namespace Rendering
} // namespace Engine