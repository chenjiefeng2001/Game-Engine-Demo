#pragma once

#include "Engine/Core/IWindow.h"
#include "Engine/Platform/GlfwWindow.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/OpenGL/OpenGLContext.h"

struct GLFWwindow;

namespace Engine {

	// ============================================================================
	// GlfwWindow - GLFW 窗口实现
	// 接收外部创建的 GLFWwindow 和 IRenderContext（依赖注入）
	// 窗口和上下文的创建逻辑由 GraphicsFactory 负责
	// ============================================================================
	class GlfwWindow : public IWindow
	{
	public:
		GlfwWindow(GLFWwindow* nativeWindow,
				   std::unique_ptr<IRenderContext> context);
		virtual ~GlfwWindow() override;

		virtual void OnUpdate() override;
		virtual bool ShouldClose() const override;
		virtual IRenderContext* GetContext() override { return m_Context.get(); }

	private:
		GLFWwindow* m_Window;
		std::unique_ptr<IRenderContext> m_Context;
	};

} // namespace Engine
