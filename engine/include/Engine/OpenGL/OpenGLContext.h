#pragma once

#include "Engine/Core/IRenderContext.h"

#include <glad/gl.h>

struct GLFWwindow;

namespace Engine {

	// ============================================================================
	// OpenGLContext - OpenGL 渲染上下文实现
	// 只包含渲染操作，资源创建由 OpenGLGraphicsFactory 负责
	// ============================================================================
	class OpenGLContext : public IRenderContext
	{
	public:
		OpenGLContext(GLFWwindow* windowHandle, GladGLContext& gl);

		virtual void Init() override;
		virtual void SwapBuffers() override;
		virtual void ClearColor(float r, float g, float b, float a) override;

		virtual void DrawIndexed(const std::shared_ptr<VertexArray>& va) override;

		GladGLContext& GetGL() { return m_GL; }

	private:
		GLFWwindow* m_WindowHandle;
		GladGLContext& m_GL;
	};

} // namespace Engine
