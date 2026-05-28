#include "Engine/OpenGL/OpenGLGraphicsFactory.h"

#include "Engine/Platform/GlfwWindow.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/Core/Memory/StackAllocatorAdaptor.h"

#include "Resources/OpenGLShader.h"
#include "Resources/OpenGLTexture.h"
#include "Resources/OpenGLVertexBuffer.h"
#include "Resources/OpenGLIndexBuffer.h"
#include "Resources/OpenGLVertexArray.h"
#include "Resources/OpenGLSpriteBatch.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <memory>

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
		int width, int height, const std::string& title)
	{
		GLFWwindow* nativeWindow = glfwCreateWindow(
			width, height, title.c_str(), nullptr, nullptr);

		if (!nativeWindow) {
			std::cerr << "[OpenGLGraphicsFactory] Failed to create GLFW window" << std::endl;
			return nullptr;
		}

		// 创建渲染上下文
		auto context = CreateRenderContext(nativeWindow);

		glfwMakeContextCurrent(nativeWindow);
		GladGLContext& ctxGL = static_cast<OpenGLContext*>(context.get())->GetGL();
		int version = gladLoadGLContext(&ctxGL, glfwGetProcAddress);
		if (version == 0) {
			std::cerr << "[OpenGLGraphicsFactory] Failed to initialize GLAD for window" << std::endl;
			glfwDestroyWindow(nativeWindow);
			return nullptr;
		}

		if (context) {
			context->Init();  // ← 现在 m_GL 函数指针已就绪，不会崩溃
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
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLShader> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLShader>(adaptor, vertexPath, fragmentPath, m_GL);
		}
		return std::make_shared<OpenGLShader>(vertexPath, fragmentPath, m_GL);
	}

	std::shared_ptr<Texture> OpenGLGraphicsFactory::CreateTexture(
		const std::string& path)
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLTexture> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLTexture>(adaptor, path, m_GL);
		}
		return std::make_shared<OpenGLTexture>(path, m_GL);
	}

	std::shared_ptr<VertexBuffer> OpenGLGraphicsFactory::CreateVertexBuffer(
		float* vertices,
		uint32_t size)
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLVertexBuffer> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLVertexBuffer>(adaptor, vertices, size, m_GL);
		}
		return std::make_shared<OpenGLVertexBuffer>(vertices, size, m_GL);
	}

	std::shared_ptr<IndexBuffer> OpenGLGraphicsFactory::CreateIndexBuffer(
		uint32_t* indices,
		uint32_t count)
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLIndexBuffer> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLIndexBuffer>(adaptor, indices, count, m_GL);
		}
		return std::make_shared<OpenGLIndexBuffer>(indices, count, m_GL);
	}

	std::shared_ptr<VertexArray> OpenGLGraphicsFactory::CreateVertexArray()
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLVertexArray> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLVertexArray>(adaptor, m_GL);
		}
		return std::make_shared<OpenGLVertexArray>(m_GL);
	}

	std::shared_ptr<ISpriteBatch> OpenGLGraphicsFactory::CreateSpriteBatch(
		IRenderContext& renderContext)
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLSpriteBatch> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLSpriteBatch>(adaptor, m_GL, renderContext);
		}
		return std::make_shared<OpenGLSpriteBatch>(m_GL, renderContext);
	}

} // namespace Engine
