#include "Engine/OpenGL/OpenGLGraphicsFactory.h"

#include "Engine/Platform/GlfwWindow.h"
#include "Engine/OpenGL/OpenGLContext.h"

#include "Engine/OpenGL/Resources/OpenGLShader.h"
#include "Engine/OpenGL/Resources/OpenGLTexture.h"
#include "Engine/OpenGL/Resources/OpenGLVertexBuffer.h"
#include "Engine/OpenGL/Resources/OpenGLIndexBuffer.h"
#include "Engine/OpenGL/Resources/OpenGLVertexArray.h"

#include <GLFW/glfw3.h>
#include <iostream>

namespace Engine {

	OpenGLGraphicsFactory::OpenGLGraphicsFactory()
	{
		// 初始化 GLFW
		if (!glfwInit())
		{
			std::cerr << "[OpenGLGraphicsFactory] Failed to initialize GLFW" << std::endl;
			return;
		}

		// 配置 OpenGL 版本
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		// 创建一个临时隐藏窗口来加载 GLAD
		// GLAD 只需加载一次，之后所有窗口共享同一个 OpenGL 上下文函数指针表
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		GLFWwindow* tempWindow = glfwCreateWindow(1, 1, "GLAD Loader", nullptr, nullptr);
		if (tempWindow)
		{
			glfwMakeContextCurrent(tempWindow);
			int version = gladLoadGLContext(&m_GL, glfwGetProcAddress);
			if (version == 0)
			{
				std::cerr << "[OpenGLGraphicsFactory] Failed to initialize GLAD" << std::endl;
			}
			else
			{
				std::cout << "[OpenGLGraphicsFactory] OpenGL " 
						  << GLAD_VERSION_MAJOR(version) << "."
						  << GLAD_VERSION_MINOR(version) << " loaded" << std::endl;
			}
			glfwDestroyWindow(tempWindow);
		}
		else
		{
			std::cerr << "[OpenGLGraphicsFactory] Failed to create temporary GLAD loader window" << std::endl;
		}


		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
	}

	OpenGLGraphicsFactory::~OpenGLGraphicsFactory()
	{
		glfwTerminate();
	}


	std::unique_ptr<IWindow> OpenGLGraphicsFactory::CreateWindow(
		int width,
		int height,
		const std::string& title)
	{
		GLFWwindow* nativeWindow = glfwCreateWindow(
			width, height, title.c_str(), nullptr, nullptr);

		if (!nativeWindow)
		{
			std::cerr << "[OpenGLGraphicsFactory] Failed to create GLFW window" << std::endl;
			return nullptr;
		}

		// 通过工厂创建渲染上下文（依赖注入）
		auto context = CreateRenderContext(nativeWindow);
		if (context)
		{
			context->Init();
		}

		return std::make_unique<GlfwWindow>(nativeWindow, std::move(context));
	}

	// ---- 渲染上下文 ----
	std::unique_ptr<IRenderContext> OpenGLGraphicsFactory::CreateRenderContext(
		void* nativeWindowHandle)
	{
		auto* window = static_cast<GLFWwindow*>(nativeWindowHandle);
		return std::make_unique<OpenGLContext>(window, m_GL);
	}

	// ---- GPU 资源 ----
	std::shared_ptr<Shader> OpenGLGraphicsFactory::CreateShader(
		const std::string& vertexPath,
		const std::string& fragmentPath)
	{
		return std::make_shared<OpenGLShader>(vertexPath, fragmentPath, m_GL);
	}

	std::shared_ptr<Texture> OpenGLGraphicsFactory::CreateTexture(
		const std::string& path)
	{
		return std::make_shared<OpenGLTexture>(path, m_GL);
	}

	std::shared_ptr<VertexBuffer> OpenGLGraphicsFactory::CreateVertexBuffer(
		float* vertices,
		uint32_t size)
	{
		return std::make_shared<OpenGLVertexBuffer>(vertices, size, m_GL);
	}

	std::shared_ptr<IndexBuffer> OpenGLGraphicsFactory::CreateIndexBuffer(
		uint32_t* indices,
		uint32_t count)
	{
		return std::make_shared<OpenGLIndexBuffer>(indices, count, m_GL);
	}

	std::shared_ptr<VertexArray> OpenGLGraphicsFactory::CreateVertexArray()
	{
		return std::make_shared<OpenGLVertexArray>(m_GL);
	}

} // namespace Engine
