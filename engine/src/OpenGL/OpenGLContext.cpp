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

	OpenGLContext::OpenGLContext(GLFWwindow* windowHandle, GladGLContext& sharedGL)
		: m_WindowHandle(windowHandle)
		, m_GL(sharedGL)       // ← 引用必须在这里初始化
	{
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
	void OpenGLContext::OnResize(int width, int height) {
		m_GL.Viewport(0, 0, width, height);
	}
	//init
	void OpenGLContext::Init()
	{
		m_GL.Enable(GL_DEPTH_TEST);
		m_GL.Enable(GL_BLEND);
		m_GL.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glfwSwapInterval(1);
	}
}
