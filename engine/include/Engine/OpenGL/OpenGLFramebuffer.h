#pragma once

/**
 * @file OpenGLFramebuffer.h
 * @brief OpenGL 帧缓冲对象（FBO）— 离屏渲染 + Entity ID Picking
 *
 * 创建三个附件：
 *   1. 颜色附件（RGBA8 纹理）— 供 ImGui::Image() 显示
 *   2. 深度模板附件（DEPTH24_STENCIL8 渲染缓冲）
 *   3. 拾取附件（R32I 纹理）— 存储每个像素的 GameObject ID
 *
 * 拾取使用方式：
 * @code
 *   // 渲染场景时，用拾取 Shader 将 GameObject ID 写入 Pick Texture
 *   // 鼠标点击时：
 *   uint32 pickedID = fbo.ReadPixelID(mouseX, mouseY);
 *   GameObject* obj = scene->FindByID(pickedID);
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

    public:
        // ── ID 拾取 ──
        /**
         * @brief 读取鼠标位置处的 GameObject ID（绝对屏幕坐标）
         * @param mouseX 绝对屏幕 X（物理像素）
         * @param mouseY 绝对屏幕 Y（物理像素）
         * @param viewportMinX 视口左上角绝对 X
         * @param viewportMinY 视口左上角绝对 Y
         * @return GameObject ID，0 表示空白
         *
         * 使用 glReadPixels 从拾取纹理（R32I）中读取像素值。
         * mouseX/mouseY 为操作系统绝对坐标，viewportMinX/Y 为视口左上角。
         */
        uint32 ReadPixelID(float mouseX, float mouseY,
                           float viewportMinX, float viewportMinY) const;

        /** 获取拾取纹理的 OpenGL ID（用于多渲染目标绑定） */
        uint32 GetPickTextureID() const { return m_PickTexture; }

        /** 是否支持 ID 拾取 */
        bool HasPickTexture() const { return m_PickTexture != 0; }

        /** 绑定拾取纹理为第二个颜色输出（在场景渲染前设置） */
        void BindForPickRendering();

    private:
        /** 销毁所有 GL 资源 */
        void Destroy();

        GladGLContext& m_GL;

        uint32 m_FBO           = 0;   // 帧缓冲对象
        uint32 m_ColorTexture  = 0;   // 颜色附件（RGBA8 纹理）
        uint32 m_DepthStencil  = 0;   // 深度模板附件（Renderbuffer）
        uint32 m_PickTexture   = 0;   // 拾取附件（R32I 纹理）

        uint32 m_Width  = 0;
        uint32 m_Height = 0;
    };

} // namespace Engine