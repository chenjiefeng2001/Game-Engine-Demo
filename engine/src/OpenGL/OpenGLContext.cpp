#include "Engine/OpenGL/OpenGLContext.h"

#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Resources/OpenGLShader.h"
#include "Resources/OpenGLTexture.h"
#include "Resources/OpenGLVertexArray.h"
#include "Resources/OpenGLVertexBuffer.h"
#include "Resources/OpenGLIndexBuffer.h"
#include <GLFW/glfw3.h>
#include <iostream>

namespace Engine {

	OpenGLContext::OpenGLContext(GLFWwindow* windowHandle)
		: m_WindowHandle(windowHandle) // m_GL 会通过默认的 m_GL{} 自动初始化
	{
	}

	void OpenGLContext::Init()
	{
		m_GL.Enable(GL_DEPTH_TEST);
		m_GL.Enable(GL_BLEND);
		m_GL.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	void OpenGLContext::SwapBuffers()
	{
		glfwSwapBuffers(m_WindowHandle);
	}

	void OpenGLContext::ClearColor(float r, float g, float b, float a)
	{
		m_GL.ClearColor(r, g, b, a);
		m_GL.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	void OpenGLContext::DrawIndexed(const std::shared_ptr<VertexArray>& va)
	{
		va->Bind();
		const auto& indexBuffer = va->GetIndexBuffer();
		if (indexBuffer)
		{
			m_GL.DrawElements(GL_TRIANGLES, indexBuffer->GetCount(), GL_UNSIGNED_INT, nullptr);
		}
	}
	std::shared_ptr<Shader> OpenGLContext::CreateShader(const std::string& vPath, const std::string& fPath) {
		return std::make_shared<OpenGLShader>(vPath, fPath, m_GL);
	}

	std::shared_ptr<Texture> OpenGLContext::CreateTexture(const std::string& path) {
		return std::make_shared<OpenGLTexture>(path, m_GL);
	}

	std::shared_ptr<VertexArray> OpenGLContext::CreateVertexArray() {
		return std::make_shared<OpenGLVertexArray>(m_GL);
	}

	std::shared_ptr<VertexBuffer> OpenGLContext::CreateVertexBuffer(float* vertices, uint32_t size) {
		return std::make_shared<OpenGLVertexBuffer>(vertices, size, m_GL);
	}

	std::shared_ptr<IndexBuffer> OpenGLContext::CreateIndexBuffer(uint32_t* indices, uint32_t count) {
		return std::make_shared<OpenGLIndexBuffer>(indices, count, m_GL);
	}
}
