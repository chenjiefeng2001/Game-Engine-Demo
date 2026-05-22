#include "Engine/Platform/GlfwWindow.h"
#include "Engine/Core/IWindow.h"
#include "Engine/OpenGL/OpenGLContext.h"

#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <iostream>

namespace Engine {

	GlfwWindow::GlfwWindow(GLFWwindow* nativeWindow,
		std::unique_ptr<IRenderContext> context)
		: m_Window(nativeWindow)
		, m_Context(std::move(context))
	{
		if (!m_Window)
		{
			std::cerr << "[GlfwWindow] Received null native window handle" << std::endl;
		}
		glfwSetWindowUserPointer(m_Window, this);  // 重要：将 this 传给回调

		glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* win, int w, int h) {
			auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(win));
			self->OnResize(w, h);
			});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* win, int key, int sc, int action, int mods) {
			auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(win));
			self->OnKey(key, sc, action, mods);
			});

		// 窗口关闭 → 可以拦截/自定义行为
		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* win) {
			auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(win));
			self->OnClose();
			});
	}
	void GlfwWindow::OnResize(int width, int height) {
		// 例如：更新 OpenGL 视口
		// 但最好委托给 IRenderContext
		if (m_Context) {
			m_Context->OnResize(width, height);  // 需要在 IRenderContext 加虚方法
		}
		// 或者通过事件回调通知 Application
		if (m_EventCallback) {
			Event e{ EventType::WindowResize };
			e.resize.width = width;
			e.resize.height = height;
			m_EventCallback(e);
		}
	}
	void GlfwWindow::OnKey(int key, int scancode, int action, int mods) {
		// 通过事件回调通知 Application
		if (m_EventCallback) {
			Event e{ EventType::KeyPress };   // 或根据 action 区分 KeyPress/KeyRelease
			e.key.key = key;
			e.key.scancode = scancode;
			e.key.action = action;
			e.key.mods = mods;
			m_EventCallback(e);
		}
	}

	void GlfwWindow::OnClose() {
		if (m_EventCallback) {
			Event e{ EventType::WindowClose };
			m_EventCallback(e);
		}
		// 默认行为：GLFW 已经设置了窗口关闭标志，不需要额外操作
		// 如果你想阻止关闭，可以调用 glfwSetWindowShouldClose(m_Window, GLFW_FALSE);
	}
	GlfwWindow::~GlfwWindow()
	{
		m_Context.reset();

		if (m_Window)
		{
			glfwDestroyWindow(m_Window);
		}
	}

	void GlfwWindow::OnUpdate()
	{
		glfwPollEvents();
		if (m_Context)
		{
			m_Context->SwapBuffers();
		}
	}

	bool GlfwWindow::ShouldClose() const
	{
		return m_Window ? glfwWindowShouldClose(m_Window) : true;
	}

	//Register the GLFW Recall


}
