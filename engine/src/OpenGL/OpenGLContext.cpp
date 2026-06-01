#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/OpenGL/OpenGLAntiAliasing.h"

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

	OpenGLContext::~OpenGLContext()
	{
		// AntiAliasing 的 unique_ptr 自动析构
	}

	void OpenGLContext::SetAntiAliasingConfig(const AntiAliasingConfig& config)
	{
		if (!m_AntiAliasing) {
			// 延迟创建 — Init() 之后才有合法的 GL 上下文
			return;
		}
		m_AntiAliasing->SetConfig(config);
	}

	const AntiAliasingConfig& OpenGLContext::GetAntiAliasingConfig() const
	{
		static AntiAliasingConfig defaultConfig;
		if (m_AntiAliasing)
			return m_AntiAliasing->GetConfig();
		return defaultConfig;
	}

	void OpenGLContext::SwapBuffers()
	{
		// 执行抗锯齿 resolve（如果启用了 AA）
		if (m_AntiAliasing && m_AntiAliasing->IsActive()) {
			m_AntiAliasing->ResolveToDefault();
		}

		glfwSwapBuffers(m_WindowHandle);
	}

	void OpenGLContext::ClearColor(float r, float g, float b, float a)
	{
		// 绑定 AA 帧缓冲（如果启用了 AA）
		if (m_AntiAliasing && m_AntiAliasing->IsActive()) {
			m_AntiAliasing->BindForRender();
		}

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

		if (m_AntiAliasing) {
			m_AntiAliasing->OnResize(width, height);
		}
	}

	bool OpenGLContext::CaptureFrameBuffer(int32& outWidth, int32& outHeight,
	                                       std::vector<uint8_t>& outPixels) {
		// 如果 AA 激活，先从 AA FBO resolve 到默认 FBO 再截屏
		if (m_AntiAliasing && m_AntiAliasing->IsActive()) {
			m_AntiAliasing->ResolveToDefault();
		}

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

	void OpenGLContext::ResolveToDefault()
	{
		if (m_AntiAliasing && m_AntiAliasing->IsActive()) {
			m_AntiAliasing->ResolveToDefault();
		} else {
			m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
		}
	}

	//init
	void OpenGLContext::Init()
	{
		m_GL.Enable(GL_DEPTH_TEST);
		m_GL.Enable(GL_BLEND);
		m_GL.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		m_GL.Enable(GL_MULTISAMPLE);  // 启用 FSAA 多重采样

		// ── 创建抗锯齿管理器（现在 GL 上下文已就绪） ──
		m_AntiAliasing = std::make_unique<OpenGLAntiAliasing>(m_GL);
		m_AntiAliasingInitialized = true;

		glfwSwapInterval(1);
	}

	// ── 深度预渲染状态控制 ──
	void OpenGLContext::SetDepthMask(bool enable) {
		m_GL.DepthMask(enable ? GL_TRUE : GL_FALSE);
	}
	void OpenGLContext::SetColorMask(bool r, bool g, bool b, bool a) {
		m_GL.ColorMask(r ? GL_TRUE : GL_FALSE, g ? GL_TRUE : GL_FALSE,
		               b ? GL_TRUE : GL_FALSE, a ? GL_TRUE : GL_FALSE);
	}
	void OpenGLContext::SetDepthFunc(uint32 func) {
		m_GL.DepthFunc(func);
	}
}
