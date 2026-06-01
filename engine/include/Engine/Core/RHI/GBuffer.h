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
        GBuffer(IRenderContext& context);
        ~GBuffer();

        GBuffer(const GBuffer&) = delete;
        GBuffer& operator=(const GBuffer&) = delete;

        bool Initialize(const GBufferConfig& cfg);
        void Shutdown();
        bool IsValid() const { return m_FBO != 0; }

        // ── 绑定 ──
        /** 绑定为渲染目标（几何通道写入） */
        void BindForGeometryPass();

        /** 解绑（回到默认 FBO） */
        void Unbind();

        /** 绑定 G-Buffer 纹理为光照通道输入 */
        void BindTexturesForLighting() const;

        // ── 清空 ──
        void Clear();

        // ── 访问器 ──
        uint32 GetFBO() const { return m_FBO; }
        uint32 GetPositionTex() const { return m_PositionTex; }
        uint32 GetNormalTex()   const { return m_NormalTex; }
        uint32 GetAlbedoTex()   const { return m_AlbedoTex; }
        uint32 GetPBRTex()      const { return m_PBRTex; }
        uint32 GetDepthTex()    const { return m_DepthTex; }

        uint32 GetWidth()  const { return m_Config.width; }
        uint32 GetHeight() const { return m_Config.height; }

    private:
        void* m_GLContext = nullptr;
        GBufferConfig m_Config;

        uint32 m_FBO       = 0;
        uint32 m_PositionTex = 0;
        uint32 m_NormalTex   = 0;
        uint32 m_AlbedoTex   = 0;
        uint32 m_PBRTex      = 0;
        uint32 m_DepthTex    = 0;
    };

} // namespace Engine
