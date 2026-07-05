#include "Engine/Editor/PickingFBO.h"
#include <glad/gl.h>
#include <algorithm>

namespace Engine {

    PickingFBO::PickingFBO(GladGLContext& gl)
        : m_GL(gl) {
    }

    PickingFBO::~PickingFBO() {
        Destroy();
    }

    PickingFBO::PickingFBO(PickingFBO&& other) noexcept
        : m_GL(other.m_GL)
        , m_FBO(other.m_FBO)
        , m_ColorTexture(other.m_ColorTexture)
        , m_DepthBuffer(other.m_DepthBuffer)
        , m_Width(other.m_Width)
        , m_Height(other.m_Height) {
        other.m_FBO = 0;
        other.m_ColorTexture = 0;
        other.m_DepthBuffer = 0;
        other.m_Width = 0;
        other.m_Height = 0;
    }

    PickingFBO& PickingFBO::operator=(PickingFBO&& other) noexcept {
        if (this != &other) {
            Destroy();
            m_FBO = other.m_FBO;
            m_ColorTexture = other.m_ColorTexture;
            m_DepthBuffer = other.m_DepthBuffer;
            m_Width = other.m_Width;
            m_Height = other.m_Height;
            other.m_FBO = 0;
            other.m_ColorTexture = 0;
            other.m_DepthBuffer = 0;
            other.m_Width = 0;
            other.m_Height = 0;
        }
        return *this;
    }

    void PickingFBO::Destroy() {
        if (m_FBO) {
            m_GL.DeleteFramebuffers(1, &m_FBO);
            m_FBO = 0;
        }
        if (m_ColorTexture) {
            m_GL.DeleteTextures(1, &m_ColorTexture);
            m_ColorTexture = 0;
        }
        if (m_DepthBuffer) {
            m_GL.DeleteRenderbuffers(1, &m_DepthBuffer);
            m_DepthBuffer = 0;
        }
        m_Width = 0;
        m_Height = 0;
    }

    void PickingFBO::Resize(uint32 width, uint32 height) {
        if (m_FBO != 0 && m_Width == width && m_Height == height)
            return;

        width  = (std::max)(width, 1u);
        height = (std::max)(height, 1u);

        Destroy();
        m_Width  = width;
        m_Height = height;

        // 颜色附件：R32UI — 存储 uint32 ID
        m_GL.GenTextures(1, &m_ColorTexture);
        m_GL.BindTexture(GL_TEXTURE_2D, m_ColorTexture);
        m_GL.TexImage2D(GL_TEXTURE_2D, 0, GL_R32UI,
                        static_cast<GLint>(width), static_cast<GLint>(height),
                        0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 深度缓冲
        m_GL.GenRenderbuffers(1, &m_DepthBuffer);
        m_GL.BindRenderbuffer(GL_RENDERBUFFER, m_DepthBuffer);
        m_GL.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                 static_cast<GLint>(width),
                                 static_cast<GLint>(height));

        // FBO
        m_GL.GenFramebuffers(1, &m_FBO);
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        m_GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, m_ColorTexture, 0);
        m_GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                     GL_RENDERBUFFER, m_DepthBuffer);

        GLenum status = m_GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            Destroy();
        }

        m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void PickingFBO::Bind() {
        if (m_FBO) {
            m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
            m_GL.Viewport(0, 0,
                          static_cast<GLint>(m_Width),
                          static_cast<GLint>(m_Height));
            // 清除为 ID 0（背景）
            GLuint clearID = 0;
            m_GL.ClearBufferuiv(GL_COLOR, 0, &clearID);
            m_GL.Clear(GL_DEPTH_BUFFER_BIT);
        }
    }

    void PickingFBO::Unbind() {
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // uint32 ID 编码为 RGB：直接用 R 通道存储 ID（R32UI）
    // 但为兼容性，也提供 RGB 编码方式
    void PickingFBO::EncodeID(uint32 id, float& r, float& g, float& b) {
        // 将 32-bit ID 拆成 3 个 8-bit 分量
        // 注意：OpenGL 输出到 RGBA8 时，值是 0-1 范围的 float
        r = ((id >> 16) & 0xFF) / 255.0f;
        g = ((id >> 8)  & 0xFF) / 255.0f;
        b = ((id)       & 0xFF) / 255.0f;
    }

    uint32 PickingFBO::ReadPixel(int32 mouseX, int32 mouseY) {
        if (!m_FBO || m_Width == 0 || m_Height == 0) return 0;

        // 将鼠标坐标翻转到 OpenGL 坐标系（左下角原点）
        GLint x = static_cast<GLint>(mouseX);
        GLint y = static_cast<GLint>(m_Height) - 1 - static_cast<GLint>(mouseY);

        // 限制到有效范围
        x = (std::max)(0, (std::min)(x, static_cast<GLint>(m_Width) - 1));
        y = (std::max)(0, (std::min)(y, static_cast<GLint>(m_Height) - 1));

        // 绑定 FBO 并读取 R32UI 像素
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        GLuint pixelID = 0;
        m_GL.ReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &pixelID);
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);

        return static_cast<uint32>(pixelID);
    }

} // namespace Engine