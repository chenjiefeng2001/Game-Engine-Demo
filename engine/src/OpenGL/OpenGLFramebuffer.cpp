#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include <glad/gl.h>
#include <algorithm>

namespace Engine {

    OpenGLFramebuffer::OpenGLFramebuffer(GladGLContext& gl)
        : m_GL(gl) {
    }

    OpenGLFramebuffer::~OpenGLFramebuffer() {
        Destroy();
    }

    OpenGLFramebuffer::OpenGLFramebuffer(OpenGLFramebuffer&& other) noexcept
        : m_GL(other.m_GL)
        , m_FBO(other.m_FBO)
        , m_ColorTexture(other.m_ColorTexture)
        , m_DepthStencil(other.m_DepthStencil)
        , m_Width(other.m_Width)
        , m_Height(other.m_Height) {
        other.m_FBO          = 0;
        other.m_ColorTexture = 0;
        other.m_DepthStencil = 0;
        other.m_Width        = 0;
        other.m_Height       = 0;
    }

    OpenGLFramebuffer& OpenGLFramebuffer::operator=(OpenGLFramebuffer&& other) noexcept {
        if (this != &other) {
            Destroy();
            m_FBO          = other.m_FBO;
            m_ColorTexture = other.m_ColorTexture;
            m_DepthStencil = other.m_DepthStencil;
            m_Width        = other.m_Width;
            m_Height       = other.m_Height;

            other.m_FBO          = 0;
            other.m_ColorTexture = 0;
            other.m_DepthStencil = 0;
            other.m_Width        = 0;
            other.m_Height       = 0;
        }
        return *this;
    }

    void OpenGLFramebuffer::Destroy() {
        if (m_FBO) {
            m_GL.DeleteFramebuffers(1, &m_FBO);
            m_FBO = 0;
        }
        if (m_ColorTexture) {
            m_GL.DeleteTextures(1, &m_ColorTexture);
            m_ColorTexture = 0;
        }
        if (m_DepthStencil) {
            m_GL.DeleteRenderbuffers(1, &m_DepthStencil);
            m_DepthStencil = 0;
        }
        if (m_PickTexture) {
            m_GL.DeleteTextures(1, &m_PickTexture);
            m_PickTexture = 0;
        }
        m_Width  = 0;
        m_Height = 0;
    }

    void OpenGLFramebuffer::Resize(uint32 width, uint32 height) {
        // 如果尺寸没变，跳过
        if (m_FBO != 0 && m_Width == width && m_Height == height)
            return;

        // 限制最小尺寸
        width  = (std::max)(width, 1u);
        height = (std::max)(height, 1u);

        // 销毁旧资源
        Destroy();

        m_Width  = width;
        m_Height = height;

        // ── 1. 创建颜色附件纹理 ──
        m_GL.GenTextures(1, &m_ColorTexture);
        m_GL.BindTexture(GL_TEXTURE_2D, m_ColorTexture);
        m_GL.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                        static_cast<GLint>(width), static_cast<GLint>(height),
                        0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // ── 2. 创建深度模板渲染缓冲 ──
        m_GL.GenRenderbuffers(1, &m_DepthStencil);
        m_GL.BindRenderbuffer(GL_RENDERBUFFER, m_DepthStencil);
        m_GL.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                                 static_cast<GLint>(width),
                                 static_cast<GLint>(height));

        // ── 3. 创建拾取纹理（R32I） ──
        m_GL.GenTextures(1, &m_PickTexture);
        m_GL.BindTexture(GL_TEXTURE_2D, m_PickTexture);
        m_GL.TexImage2D(GL_TEXTURE_2D, 0, GL_R32I,
                        static_cast<GLint>(width), static_cast<GLint>(height),
                        0, GL_RED_INTEGER, GL_INT, nullptr);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // ── 4. 创建 FBO ──
        m_GL.GenFramebuffers(1, &m_FBO);
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);

        // 附加颜色纹理 (GL_COLOR_ATTACHMENT0)
        m_GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, m_ColorTexture, 0);
        // 附加拾取纹理 (GL_COLOR_ATTACHMENT1)
        m_GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                                  GL_TEXTURE_2D, m_PickTexture, 0);
        // 附加深度模板
        m_GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                     GL_RENDERBUFFER, m_DepthStencil);

        // 设置多渲染目标：写入两个颜色附件
        GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        m_GL.DrawBuffers(2, drawBuffers);

        // ── 5. 检查完整性 ──
        GLenum status = m_GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            Destroy();
        }

        // 解绑
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::Bind() {
        if (m_FBO) {
            m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
            m_GL.Viewport(0, 0,
                          static_cast<GLint>(m_Width),
                          static_cast<GLint>(m_Height));
        }
    }

    void OpenGLFramebuffer::Unbind() {
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ═══════════════════════════════════════════════════════════════
    // ID 拾取 — glReadPixels 从拾取纹理读取像素
    // ═══════════════════════════════════════════════════════════════

    void OpenGLFramebuffer::BindForPickRendering() {
        // 绑定 FBO 后，设置只写入 GL_COLOR_ATTACHMENT1（拾取纹理）
        // 这样场景渲染的片段 Shader 可以将 entity ID 输出到 layout(location=1)
        // 注意：调用前需要已经 Bind() 了 FBO
        if (!m_FBO) return;

        GLenum drawBuffers[2] = { GL_NONE, GL_COLOR_ATTACHMENT1 };
        m_GL.DrawBuffers(2, drawBuffers);
    }

    uint32 OpenGLFramebuffer::ReadPixelID(float mouseX, float mouseY,
                                           float viewportMinX,
                                           float viewportMinY) const {
        if (!m_PickTexture || !m_FBO) return 0;

        // 将 OS 绝对坐标转换为 FBO 内部坐标
        // ImGui 的坐标系 Y 轴向下，OpenGL 的 Y 轴向上，需要翻转
        int px = static_cast<int>(mouseX - viewportMinX);
        int py = static_cast<int>(viewportMinY - mouseY + static_cast<float>(m_Height));

        // 边界检查
        if (px < 0 || px >= static_cast<int>(m_Width) ||
            py < 0 || py >= static_cast<int>(m_Height)) {
            return 0;
        }

        // 绑定 FBO 并读取拾取纹理的一个像素
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        m_GL.ReadBuffer(GL_COLOR_ATTACHMENT1);

        int32 pixelValue = 0;
        m_GL.ReadPixels(px, py, 1, 1, GL_RED_INTEGER, GL_INT, &pixelValue);

        m_GL.ReadBuffer(GL_COLOR_ATTACHMENT0);  // 恢复默认读取缓冲区
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0); // 解绑

        return static_cast<uint32>(std::max(0, pixelValue));
    }

} // namespace Engine
