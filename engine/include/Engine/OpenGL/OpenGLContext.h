#pragma once
#include "Engine/Core/IRenderContext.h"
#include <glad/gl.h>

// 前向声明 glfw 窗口指针
struct GLFWwindow;

namespace Engine {
    class OpenGLContext : public IRenderContext {
    public:
        OpenGLContext(GLFWwindow* windowHandle);
        virtual ~OpenGLContext() override = default;

        // --- 严格重写 9 个父类方法 ---
        virtual void Init() override;
        virtual void ClearColor(float r, float g, float b, float a) override;
        virtual void SwapBuffers() override;
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& va) override;

        virtual std::shared_ptr<Shader> CreateShader(const std::string& vPath, const std::string& fPath) override;
        virtual std::shared_ptr<Texture> CreateTexture(const std::string& path) override;
        virtual std::shared_ptr<VertexArray> CreateVertexArray() override;
        virtual std::shared_ptr<VertexBuffer> CreateVertexBuffer(float* vertices, uint32_t size) override;
        virtual std::shared_ptr<IndexBuffer> CreateIndexBuffer(uint32_t* indices, uint32_t count) override;

        // 获取 GLAD 实例
        GladGLContext& GetGL() { return m_GL; }

    private:
        GLFWwindow* m_WindowHandle;
        GladGLContext m_GL{};
    };
}