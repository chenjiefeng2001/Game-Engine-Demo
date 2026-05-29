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
		, m_GL(sharedGL)
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
			++m_DrawCallCount;
		}
	}
	void OpenGLContext::OnResize(int width, int height) {
		m_GL.Viewport(0, 0, width, height);
	}

	bool OpenGLContext::CaptureFrameBuffer(int32& outWidth, int32& outHeight,
	                                       std::vector<uint8_t>& outPixels) {
		GLint viewport[4] = {};
		m_GL.GetIntegerv(GL_VIEWPORT, viewport);
		outWidth  = viewport[2];
		outHeight = viewport[3];
		if (outWidth <= 0 || outHeight <= 0) return false;

		outPixels.resize(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 3);
		m_GL.ReadPixels(0, 0, outWidth, outHeight, GL_RGB, GL_UNSIGNED_BYTE,
		                outPixels.data());
		return true;
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
