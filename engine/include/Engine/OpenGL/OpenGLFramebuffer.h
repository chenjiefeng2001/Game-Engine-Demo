#pragma once

/**
 * @file OpenGLFramebuffer.h
 * @brief OpenGL 帧缓冲对象（FBO）— 用于离屏渲染
 *
 * 创建颜色附件（RGBA8 纹理）+ 深度附件（DEPTH24_STENCIL8 渲染缓冲）。
 * 渲染到 FBO 后，颜色纹理可直接传给 ImGui::Image() 在视口面板显示。
 *
 * 使用方式：
 * @code
 *   OpenGLFramebuffer fbo(gl);
 *   fbo.Resize(viewportWidth, viewportHeight);
 *
 *   // 渲染
 *   fbo.Bind();
 *   RenderScene();
 *   fbo.Unbind();
 *
 *   // 显示
 *   ImGui::Image((ImTextureID)(uint64)fbo.GetColorTextureID(),
 *                ImVec2(width, height));
 * @endcode
 */

#include "Engine/Types.h"

struct GladGLContext;

namespace Engine {

    class OpenGLFramebuffer {
    public:
        OpenGLFramebuffer(GladGLContext& gl);
        ~OpenGLFramebuffer();

        OpenGLFramebuffer(const OpenGLFramebuffer&) = delete;
        OpenGLFramebuffer& operator=(const OpenGLFramebuffer&) = delete;

        // 移动语义
        OpenGLFramebuffer(OpenGLFramebuffer&& other) noexcept;
        OpenGLFramebuffer& operator=(OpenGLFramebuffer&& other) noexcept;

        // ── 生命周期 ──
        /** 创建或重新分配 FBO 为指定尺寸（若尺寸没变则跳过） */
        void Resize(uint32 width, uint32 height);

        /** 获取当前 FBO 宽度 */
        uint32 GetWidth() const { return m_Width; }

        /** 获取当前 FBO 高度 */
        uint32 GetHeight() const { return m_Height; }

        // ── 绑定 ──
        /** 绑定 FBO 为当前渲染目标 */
        void Bind();

        /** 解绑 FBO，恢复到默认帧缓冲 */
        void Unbind();

        // ── 纹理访问 ──
        /** 获取颜色附件的 OpenGL 纹理 ID（用于 ImGui::Image） */
        uint32 GetColorTextureID() const { return m_ColorTexture; }

        /** 检查 FBO 是否有效 */
        bool IsValid() const { return m_FBO != 0; }

    private:
        /** 销毁所有 GL 资源 */
        void Destroy();

        GladGLContext& m_GL;

        uint32 m_FBO           = 0;   // 帧缓冲对象
        uint32 m_ColorTexture  = 0;   // 颜色附件（RGBA8 纹理）
        uint32 m_DepthStencil  = 0;   // 深度模板附件（Renderbuffer）

        uint32 m_Width  = 0;
        uint32 m_Height = 0;
    };

} // namespace Engine