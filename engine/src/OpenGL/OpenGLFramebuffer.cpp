#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <exception>

namespace Engine {

    OpenGLFramebuffer::OpenGLFramebuffer(GladGLContext& gl)
        : m_GL(gl)
        , m_OwnerThreadId(std::this_thread::get_id()) {
    }

    OpenGLFramebuffer::~OpenGLFramebuffer() {
        Destroy();
    }

    OpenGLFramebuffer::OpenGLFramebuffer(OpenGLFramebuffer&& other) noexcept
        : m_GL(other.m_GL)
        , m_OwnerThreadId(other.m_OwnerThreadId)
        , m_FBO(other.m_FBO)
        , m_ColorTexture(other.m_ColorTexture)
        , m_DepthStencil(other.m_DepthStencil)
        , m_PickTexture(other.m_PickTexture)
        , m_Width(other.m_Width)
        , m_Height(other.m_Height) {
        other.m_FBO          = 0;
        other.m_ColorTexture = 0;
        other.m_DepthStencil = 0;
        other.m_PickTexture  = 0;
        other.m_Width        = 0;
        other.m_Height       = 0;
    }

    OpenGLFramebuffer& OpenGLFramebuffer::operator=(OpenGLFramebuffer&& other) noexcept {
        if (this != &other) {
            Destroy();
            m_OwnerThreadId = other.m_OwnerThreadId;
            m_FBO          = other.m_FBO;
            m_ColorTexture = other.m_ColorTexture;
            m_DepthStencil = other.m_DepthStencil;
            m_PickTexture  = other.m_PickTexture;
            m_Width        = other.m_Width;
            m_Height       = other.m_Height;

            other.m_FBO          = 0;
            other.m_ColorTexture = 0;
            other.m_DepthStencil = 0;
            other.m_PickTexture  = 0;
            other.m_Width        = 0;
            other.m_Height       = 0;
        }
        return *this;
    }

    void OpenGLFramebuffer::Abandon() {
        if (!IsOwnerThread()) return;
        m_FBO = 0;
        m_ColorTexture = 0;
        m_DepthStencil = 0;
        m_PickTexture = 0;
        m_Width = 0;
        m_Height = 0;
    }

    void OpenGLFramebuffer::Destroy() {
        if (!IsOwnerThread()) {
            Abandon();
            return;
        }
        if (!glfwGetCurrentContext()) {
            Abandon();
            return;
        }
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
        if (!IsOwnerThread() || !glfwGetCurrentContext()) return;
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
        if (!IsOwnerThread() || !glfwGetCurrentContext()) return;
        if (m_FBO) {
            m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
            m_GL.Viewport(0, 0,
                          static_cast<GLint>(m_Width),
                          static_cast<GLint>(m_Height));
        }
    }

    void OpenGLFramebuffer::Unbind() {
        if (!IsOwnerThread() || !glfwGetCurrentContext()) return;
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::Clear(float red, float green, float blue, float alpha) {
        if (!IsOwnerThread() || !glfwGetCurrentContext() || !m_FBO) return;
        Bind();
        m_GL.ClearColor(red, green, blue, alpha);
        m_GL.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        Unbind();
    }

    bool OpenGLFramebuffer::ReadColorPixel(uint32 x, uint32 y, uint8_t out[4]) const {
        if (!IsOwnerThread() || !glfwGetCurrentContext() || !m_FBO || !out ||
            x >= m_Width || y >= m_Height) {
            return false;
        }
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        m_GL.ReadBuffer(GL_COLOR_ATTACHMENT0);
        m_GL.ReadPixels(static_cast<GLint>(x), static_cast<GLint>(y), 1, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, out);
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return m_GL.GetError() == GL_NO_ERROR;
    }

    // ═══════════════════════════════════════════════════════════════
    // ID 拾取 — glReadPixels 从拾取纹理读取像素
    // ═══════════════════════════════════════════════════════════════

    void OpenGLFramebuffer::BindForPickRendering() {
        if (!IsOwnerThread() || !glfwGetCurrentContext()) return;
        // 绑定 FBO 后，设置只写入 GL_COLOR_ATTACHMENT1（拾取纹理）
        // 这样场景渲染的片段 Shader 可以将 entity ID 输出到 layout(location=1)
        // 注意：调用前需要已经 Bind() 了 FBO
        if (!m_FBO) return;

        GLenum drawBuffers[2] = { GL_NONE, GL_COLOR_ATTACHMENT1 };
        m_GL.DrawBuffers(2, drawBuffers);
    }

    OpenGLFramebuffer::PickReadback
    OpenGLFramebuffer::ReadPickPixel(float mouseX, float mouseY,
                                    float viewportMinX,
                                    float viewportMinY) const {
        // D3-C6：失败与 no-hit 必须是不同状态，绝不再合并成 0。
        // D3-C5：owner thread + current context 守卫（与 P1 B2 同类约束）。
        if (!IsOwnerThread() || !glfwGetCurrentContext() ||
            !m_PickTexture || !m_FBO) {
            return PickReadback{PickReadbackStatus::NotReady, 0};
        }

        // D3-C3：非法坐标显式拒绝，不做 clamp。
        if (!std::isfinite(mouseX) || !std::isfinite(mouseY)) {
            return PickReadback{PickReadbackStatus::InvalidCoordinate, 0};
        }

        // D3-C3（executable 证明后冻结）：canonical 坐标为 **top-left 原点**，
        // 读回时取其严格逆映射 py = H - 1 - y。
        //
        // 旧实现是 `H - y`（树内另一处遗留写法），**差一行**：canonical y=0 会
        // 映射到 py=H 越界、整列不可达，且每一个采样都向上偏移一行。
        // 这不是风格问题 —— 它让 pick 结果与鼠标实际所在像素系统性错位一行。
        // 该结论由 tests/test_pick 的 known-pixel 判别用例实测得出，不是推导。
        const int px = static_cast<int>(mouseX - viewportMinX);
        const int py = static_cast<int>(viewportMinY - mouseY +
                                        static_cast<float>(m_Height - 1));

        // 越界一律拒绝（D3-C3）。旧的 clamp 行为会把越界点击变成边缘像素的
        // 假命中，因此这里只拒绝、不夹取。
        if (px < 0 || px >= static_cast<int>(m_Width) ||
            py < 0 || py >= static_cast<int>(m_Height)) {
            return PickReadback{PickReadbackStatus::OutsideViewport, 0};
        }

        m_GL.BindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        m_GL.ReadBuffer(GL_COLOR_ATTACHMENT1);


        int32 pixelValue = 0;
        m_GL.ReadPixels(px, py, 1, 1, GL_RED_INTEGER, GL_INT, &pixelValue);

        m_GL.ReadBuffer(GL_COLOR_ATTACHMENT0);  // 恢复默认读取缓冲区
        m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0); // 解绑

        // D3-C2：sentinel = -1 表示正常未命中。
        // 关键：这里**不能**写 std::max(0, pixelValue) —— 那会把 -1 变成 0，
        // 直接摧毁 sentinel（Step 2 废止条款 2）。
        if (pixelValue >= 1) {
            return PickReadback{PickReadbackStatus::Hit,
                                static_cast<uint32>(pixelValue)};
        }
        // -1 = sentinel no-hit；0 及其他负值 = reserved/未写入，
        // 一律按 no-hit 处理，但不再与"读取失败"混同。
        return PickReadback{PickReadbackStatus::NoHit, 0};
    }


} // namespace Engine
