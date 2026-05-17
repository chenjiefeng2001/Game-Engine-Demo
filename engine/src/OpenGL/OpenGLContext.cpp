#include "Engine/OpenGL/OpenGLContext.h"

#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"

#include <GLFW/glfw3.h>
#include <iostream>

namespace Engine {

	OpenGLContext::OpenGLContext(GLFWwindow* windowHandle, GladGLContext& gl)
		: m_WindowHandle(windowHandle)
		, m_GL(gl)
	{

		glfwMakeContextCurrent(m_WindowHandle);

		std::cout << "[OpenGLContext] Bound to OpenGL context" << std::endl;
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

}
