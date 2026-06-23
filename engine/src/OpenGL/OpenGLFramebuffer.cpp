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

        // ── 3. 创建 FBO ──
        m_GL.GenFramebuffers(1, &m_FBO);
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);

        // 附加颜色纹理
        m_GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, m_ColorTexture, 0);
        // 附加深度模板
        m_GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                     GL_RENDERBUFFER, m_DepthStencil);

        // ── 4. 检查完整性 ──
        GLenum status = m_GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            // 完整性检查失败，销毁并记录
            Destroy();
        }

        // 解绑（调用方会在需要时 Bind）
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

} // namespace Engine