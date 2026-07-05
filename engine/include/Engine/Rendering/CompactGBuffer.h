#pragma once

/**
 * @file CompactGBuffer.h
 * @brief 紧凑 G-Buffer 布局定义 — 减少显存带宽占用的 PBR 管线
 *
 * 设计理念：
 *   传统的每通道 16/32-bit 浮点 G-Buffer 显存带宽过高。
 *   本布局通过 Octahedron 法线编码 + 紧凑 Packing 在线性存储中节省约 60% 带宽。
 *
 * G-Buffer 布局对比：
 *
 *   ── 旧布局 (128 bpp) ──
 *   RT0: WorldPos    RGBA32F → 16 bytes/pixel
 *   RT1: Normal      RGBA16F →  8 bytes/pixel
 *   RT2: Albedo      RGBA8   →  4 bytes/pixel
 *   RT3: PBR         RGBA8   →  4 bytes/pixel
 *   总计: 32 bytes/pixel (1920x1080 = 63 MB)
 *
 *   ── 新布局 (64 bpp) ──
 *   RT0: Albedo      RGB10A2  →  4 bytes/pixel  R=BaseColor, G=Occlusion, B=Metallic, A=(unused)
 *   RT1: Normal+Rgh  RGB10A2  →  4 bytes/pixel  R=NormalX(enc), G=NormalY(enc), B=Roughness, A=MaterialMask
 *   RT2: Emission+   RGBA8    →  4 bytes/pixel  R=Emissive, G=(unused), B=ClearCoat, A=ShadingModelID
 *   Depth:           D24S8    →  4 bytes/pixel
 *   总计: 16 bytes/pixel (1920x1080 = 32 MB) — 节省 50% 显存带宽
 *
 * Octahedron Normal 编码 / 解码：
 *   encode(normal) → vec2(0..1)
 *   decode(vec2)   → normalized vec3
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/RHITypes.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace Engine {
namespace Rendering {

    // ============================================================
    // 紧凑 G-Buffer 布局描述
    // ============================================================
    enum class GBufferLayout : uint8 {
        Legacy   = 0,  ///< WorldPos(RGBA32F) + Normal(RGBA16F) + Albedo(RGBA8) + PBR(RGBA8)
        Compact  = 1,  ///< Albedo(RGB10A2) + NormalRgh(RGB10A2) + Emissive(RGBA8)
    };

    // ============================================================
    // Compact GBuffer 资源名（用于 RenderGraph 声明）
    // ============================================================
    namespace GBufferNames {
        constexpr const char* AlbedoAO    = "GBuffer_AlbedoAO";
        constexpr const char* NormalRgh   = "GBuffer_NormalRgh";
        constexpr const char* Emissive    = "GBuffer_Emissive";
        constexpr const char* Depth       = "GBuffer_Depth";
        constexpr const char* Velocity    = "GBuffer_Velocity";  // 运动矢量（TAA 用）
    }

    // ============================================================
    // Octahedron 法线编码（CryEngine / Unreal 标准实现）
    // ============================================================
    /**
     * @brief 将单位法线编码为 2 分量 [0, 1] 范围
     *
     * 精度：16-bit 分量时约 10^5 级精度
     */
    inline void EncodeOctNormal(float nx, float ny, float nz, float& outX, float& outY) noexcept {
        float absSum = std::abs(nx) + std::abs(ny) + std::abs(nz);
        float cx = nx / absSum;
        float cy = ny / absSum;
        // 镜像映射：将背面法线映射到正面
        if (nz < 0.0f) {
            float tcx = (1.0f - std::abs(cy)) * (cx >= 0.0f ? 1.0f : -1.0f);
            float tcy = (1.0f - std::abs(cx)) * (cy >= 0.0f ? 1.0f : -1.0f);
            cx = tcx; cy = tcy;
        }
        outX = cx * 0.5f + 0.5f;
        outY = cy * 0.5f + 0.5f;
    }

    /**
     * @brief 从编码数据解码单位法线
     */
    inline void DecodeOctNormal(float encX, float encY, float& nx, float& ny, float& nz) noexcept {
        float cx = encX * 2.0f - 1.0f;
        float cy = encY * 2.0f - 1.0f;
        float cz = 1.0f - std::abs(cx) - std::abs(cy);
        if (cz < 0.0f) {
            float tcx = (1.0f - std::abs(cy)) * (cx >= 0.0f ? 1.0f : -1.0f);
            float tcy = (1.0f - std::abs(cx)) * (cy >= 0.0f ? 1.0f : -1.0f);
            cx = tcx; cy = tcy;
        }
        // 归一化
        float len = std::sqrt(cx * cx + cy * cy + cz * cz);
        nx = cx / len;
        ny = cy / len;
        nz = cz / len;
    }

    // ============================================================
    // Compact G-Buffer 填充器 — 将 PBR 参数打包到像素格式
    // ============================================================
    /**
     * @brief 将 PBR 材质参数 + 表面法线打包到 Compact GBuffer 格式
     *
     * 输出格式：
     *   RT0 (RGB10A2): R=BaseColor.r(10bit), G=BaseColor.g(10bit),
     *                  B=BaseColor.b(10bit), A=AmbientOcclusion(2bit)
     *   RT1 (RGB10A2): R=EncNormalX(10bit), G=EncNormalY(10bit),
     *                  B=Roughness(10bit), A=MaterialMask(2bit) — 0=Opaque, 1=Masked, 2=Translucent
     *
     * 此函数供着色器实现参考，CPU 端仅用于验证和调试。
     */
    inline void PackGBuffer_Compact(
        float baseColor[3], float ao, float metallic, float roughness,
        float normalNx, float normalNy, bool opaque,
        uint32_t& outRT0, uint32_t& outRT1)
    {
        // RT0: R10G10B10A2
        uint32_t r = static_cast<uint32_t>(std::clamp(baseColor[0], 0.0f, 1.0f) * 1023.0f + 0.5f);
        uint32_t g = static_cast<uint32_t>(std::clamp(baseColor[1], 0.0f, 1.0f) * 1023.0f + 0.5f);
        uint32_t b = static_cast<uint32_t>(std::clamp(baseColor[2], 0.0f, 1.0f) * 1023.0f + 0.5f);
        uint32_t a = static_cast<uint32_t>(std::clamp(ao, 0.0f, 1.0f) * 3.0f + 0.5f);
        outRT0 = (r << 22) | (g << 12) | (b << 2) | a;

        // RT1: R10G10B10A2
        float encX, encY;
        EncodeOctNormal(normalNx, normalNy, 1.0f - normalNx*normalNx - normalNy*normalNy, encX, encY);
        uint32_t nr = static_cast<uint32_t>(encX * 1023.0f);
        uint32_t ng = static_cast<uint32_t>(encY * 1023.0f);
        uint32_t nb = static_cast<uint32_t>(std::clamp(roughness, 0.0f, 1.0f) * 1023.0f);
        uint32_t matMask = opaque ? 0 : 1;  // 0=Opaque, 1=Masked
        outRT1 = (nr << 22) | (ng << 12) | (nb << 2) | matMask;
    }

    // ============================================================
    // G-Buffer 配置
    // ============================================================
    struct GBufferConfig {
        GBufferLayout layout      = GBufferLayout::Compact;
        uint32_t      width        = 1920;
        uint32_t      height       = 1080;
        bool          includeVelocity = false;   ///< 是否包含运动矢量 pass（TAA 需要）
        RHI::Format   depthFormat  = RHI::Format::D24_UNorm_S8_UInt;
    };

    /** 计算 G-Buffer 的字节总占用 */
    inline uint32_t CalculateGBufferBytes(const GBufferConfig& cfg) noexcept {
        uint32_t bytesPerPixel = 0;
        switch (cfg.layout) {
            case GBufferLayout::Legacy:
                bytesPerPixel = 16 + 8 + 4 + 4;  // RGBA32F + RGBA16F + RGBA8 + RGBA8
                break;
            case GBufferLayout::Compact:
                bytesPerPixel = 4 + 4 + 4;       // RGB10A2 + RGB10A2 + RGBA8
                break;
        }
        if (cfg.includeVelocity) bytesPerPixel += 8;   // RG16F
        bytesPerPixel += 4;                             // D24S8
        return cfg.width * cfg.height * bytesPerPixel;
    }

} // namespace Rendering
} // namespace Engine