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
	std::unique_ptr<IWindow> IWindow::Create(uint32_t width, uint32_t height, const std::string& title)
	{
		// 1. 初始化 GLFW
		if (!glfwInit())
		{
			std::cerr << "Failed to initialize GLFW!" << std::endl;
			return nullptr;
		}

		// 设置 OpenGL 版本 (4.6 Core)
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		// 2. 调用 GLFW API 创建真实的窗口句柄
		GLFWwindow* window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
		if (!window)
		{
			std::cerr << "Failed to create GLFW window!" << std::endl;
			glfwTerminate();
			return nullptr;
		}

        // 3. 创建对应的渲染上下文 (目前写死为 OpenGL，未来可以用 #ifdef 切换 Vulkan)
		auto context = std::make_unique<OpenGLContext>(window);

		// ⚠️ 极其关键的一步：在加载任何 GL 函数之前，首先将 GLFW 上下文设为当前并加载 GLAD
		glfwMakeContextCurrent(window);
		int version = gladLoadGLContext(&context->GetGL(), glfwGetProcAddress);
		if (version == 0)
		{
			std::cerr << "Failed to initialize GLAD in IWindow::Create" << std::endl;
			glfwDestroyWindow(window);
			glfwTerminate();
			return nullptr;
		}

		// 初始化上下文（使用已加载的 m_GL）
		context->Init();

		// 4. 将句柄和上下文注入到你的 GlfwWindow 包装类中
		return std::make_unique<GlfwWindow>(window, std::move(context));
	}


}
