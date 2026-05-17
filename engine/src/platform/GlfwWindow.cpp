#include "Engine/Platform/GlfwWindow.h"

#include <GLFW/glfw3.h>
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

}
