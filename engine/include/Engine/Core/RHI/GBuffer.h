#pragma once
/**
 * @file GBuffer.h
 * @brief 几何缓冲 (G-Buffer) — 延迟渲染的多渲染目标 FBO
 *
 * G-Buffer 布局:
 *   RT0: WorldPosition (RGB16F) — 世界空间位置
 *   RT1: Normal (RGBA16F)      — 世界空间法线 + roughness
 *   RT2: Albedo (RGBA8)        — 漫反射颜色 + metallic
 *   RT3: PBR (RGBA8)           — R:ao, G:specular, B:unused, A:unused
 *   Depth: 共享深度缓冲
 *
 * 纯抽象接口 — 不包含任何后端特定类型或实现。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <cstdint>

namespace Engine {

    class IRenderContext;
    class Shader;

    struct GBufferConfig {
        uint32 width  = 1280;
        uint32 height = 720;
    };

    class GBuffer {
    public:
        GBuffer() = default;
        virtual ~GBuffer() = default;

        GBuffer(const GBuffer&) = delete;
        GBuffer& operator=(const GBuffer&) = delete;

        virtual bool Initialize(const GBufferConfig& cfg) = 0;
        virtual void Shutdown() = 0;
        virtual bool IsValid() const = 0;

        virtual void BindForGeometryPass() = 0;
        virtual void Unbind() = 0;
        virtual void BindTexturesForLighting() const = 0;
        virtual void Clear() = 0;

        virtual uint32 GetFBO() const = 0;
        virtual uint32 GetPositionTex() const = 0;
        virtual uint32 GetNormalTex()   const = 0;
        virtual uint32 GetAlbedoTex()   const = 0;
        virtual uint32 GetPBRTex()      const = 0;
        virtual uint32 GetDepthTex()    const = 0;

        virtual uint32 GetWidth()  const = 0;
        virtual uint32 GetHeight() const = 0;
    };

} // namespace Engine