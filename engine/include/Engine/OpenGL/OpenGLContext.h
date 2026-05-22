#pragma once
#include "Engine/Core/IRenderContext.h"
#include <glad/gl.h>

struct GLFWwindow;

namespace Engine {
    class OpenGLContext : public IRenderContext {
    public:
        OpenGLContext(GLFWwindow* windowHandle, GladGLContext& sharedGL);
        virtual ~OpenGLContext() override = default;

        virtual void Init() override;
        virtual void ClearColor(float r, float g, float b, float a) override;
        virtual void SwapBuffers() override;
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& va) override;
        virtual void OnResize(int width, int height) override;  // ← 新增

        GladGLContext& GetGL() { return m_GL; }

    private:
        GLFWwindow* m_WindowHandle;
        GladGLContext& m_GL;
    };
}