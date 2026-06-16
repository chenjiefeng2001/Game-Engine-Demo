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
#include <cstring>

namespace Engine {

	OpenGLContext::OpenGLContext(GLFWwindow* windowHandle, GladGLContext& sharedGL)
		: m_WindowHandle(windowHandle)
		, m_GL(sharedGL)
	{
		std::memset(m_GPUQueryObjects, 0, sizeof(m_GPUQueryObjects));
	}

	OpenGLContext::~OpenGLContext()
	{
		CleanupGPUQueries();
	}

	void OpenGLContext::SetAntiAliasingConfig(const AntiAliasingConfig& config)
	{
		if (!m_AntiAliasing) {
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
		if (m_AntiAliasing && m_AntiAliasing->IsActive()) {
			m_AntiAliasing->ResolveToDefault();
		}

		if (m_GPUQueriesReady && m_ActiveQueryCount > 0) {
			GPUProfileFrame frame;
			for (uint32 i = 0; i < m_ActiveQueryCount; ++i) {
				auto& query = m_ActiveQueries[i];
				if (!query.active) continue;

				GLuint beginDone = 0, endDone = 0;
				m_GL.GetQueryObjectiv(query.queryIndex[0],
					GL_QUERY_RESULT_AVAILABLE, (GLint*)&beginDone);
				m_GL.GetQueryObjectiv(query.queryIndex[1],
					GL_QUERY_RESULT_AVAILABLE, (GLint*)&endDone);

				if (beginDone && endDone && frame.passCount < GPUProfileFrame::kMaxPasses) {
					uint64 beginTime = 0, endTime = 0;
					m_GL.GetQueryObjectui64v(query.queryIndex[0],
						GL_QUERY_RESULT, &beginTime);
					m_GL.GetQueryObjectui64v(query.queryIndex[1],
						GL_QUERY_RESULT, &endTime);

					auto& record = frame.passes[frame.passCount++];
					record.passName     = query.passName;
					record.beginTimestamp = beginTime;
					record.endTimestamp   = endTime;
					record.elapsedMs    = static_cast<float>((endTime - beginTime)) / 1'000'000.0f;
				}
				query.active = false;
			}
			m_LastGPUProfile = frame;
			m_ActiveQueryCount = 0;
		}

		glfwSwapBuffers(m_WindowHandle);
	}

	void OpenGLContext::ClearColor(float r, float g, float b, float a)
	{
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
			uint32 indexCount = indexBuffer->GetCount();
			m_GL.DrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
			++m_DrawCallCount;

			uint32 triCount = indexCount / 3;
			m_TriangleCount += triCount;
			m_VertexCount += indexCount;
		}
	}

	void OpenGLContext::OnResize(int width, int height) {
		m_BackbufferWidth  = width;
		m_BackbufferHeight = height;
		m_GL.Viewport(0, 0, width, height);

		if (m_AntiAliasing) {
			m_AntiAliasing->OnResize(width, height);
		}
	}

	void OpenGLContext::ResetPipelineState() {
		m_GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
		m_GL.Disable(GL_DEPTH_TEST);
		m_GL.Disable(GL_CULL_FACE);
		m_GL.Enable(GL_BLEND);
		m_GL.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		m_GL.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		m_GL.Viewport(0, 0, m_BackbufferWidth, m_BackbufferHeight);
		m_GL.Disable(GL_STENCIL_TEST);
		m_GL.Disable(GL_SCISSOR_TEST);
	}

	bool OpenGLContext::CaptureFrameBuffer(int32& outWidth, int32& outHeight,
	                                       std::vector<uint8_t>& outPixels) {
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

	void OpenGLContext::Init()
	{
		m_GL.Enable(GL_DEPTH_TEST);
		m_GL.Enable(GL_BLEND);
		m_GL.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		m_GL.Enable(GL_MULTISAMPLE);

		m_AntiAliasing = std::make_unique<OpenGLAntiAliasing>(m_GL);
		m_AntiAliasingInitialized = true;

		InitGPUQueries();

		glfwSwapInterval(1);
	}

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

	void OpenGLContext::SetViewMode(ViewMode mode) {
		IRenderContext::SetViewMode(mode);

		if (mode == ViewMode::Wireframe) {
			m_GL.PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		} else {
			m_GL.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}

		if (mode == ViewMode::Wireframe) {
			m_GL.Disable(GL_CULL_FACE);
		} else {
			m_GL.Enable(GL_CULL_FACE);
		}
	}

	void OpenGLContext::BindViewModeUniform(const Shader* shader) {
		if (!shader) return;

		GLuint programID = static_cast<GLuint>(shader->GetNativeHandle());
		if (programID == 0) return;

		GLint location = m_GL.GetUniformLocation(programID, "u_ViewMode");
		if (location != -1) {
			m_GL.Uniform1i(location, ViewModeToShaderInt(m_ViewMode));
		}

		int32 pbrChannel = 0;
		switch (m_ViewMode) {
			case ViewMode::PBR_BaseColor: pbrChannel = 0; break;
			case ViewMode::PBR_Roughness: pbrChannel = 1; break;
			case ViewMode::PBR_Metallic:  pbrChannel = 2; break;
			case ViewMode::PBR_Specular:  pbrChannel = 3; break;
			default: break;
		}
		GLint pbrLoc = m_GL.GetUniformLocation(programID, "u_PBRChannel");
		if (pbrLoc != -1) {
			if (pbrChannel > 0 || m_ViewMode == ViewMode::PBR_BaseColor) {
				m_GL.Uniform1i(pbrLoc, pbrChannel);
			} else {
				m_GL.Uniform1i(pbrLoc, -1);
			}
		}
	}

	bool OpenGLContext::ReadDepthBuffer(std::vector<float>& outDepth, int32& outW, int32& outH) {
		GLint viewport[4] = {};
		m_GL.GetIntegerv(GL_VIEWPORT, viewport);
		outW = viewport[2];
		outH = viewport[3];
		if (outW <= 0 || outH <= 0) return false;

		outDepth.resize(static_cast<size_t>(outW) * static_cast<size_t>(outH));
		m_GL.ReadPixels(0, 0, outW, outH, GL_DEPTH_COMPONENT, GL_FLOAT, outDepth.data());

		for (size_t i = 0; i < outDepth.size(); ++i) {
			outDepth[i] = 1.0f - outDepth[i];
		}

		return true;
	}

	bool OpenGLContext::InitGPUQueries() {
		if (m_GPUQueriesReady) return true;

		GLint bits = 0;
		m_GL.GetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
		if (bits == 0) {
			return false;
		}

		m_GL.GenQueries(kMaxGPUQueries, m_GPUQueryObjects);
		m_GPUQueryCount = kMaxGPUQueries;
		m_GPUQueriesReady = true;
		return true;
	}

	void OpenGLContext::CleanupGPUQueries() {
		if (m_GPUQueryCount > 0 && m_GPUQueryObjects[0] != 0) {
			m_GL.DeleteQueries(m_GPUQueryCount, m_GPUQueryObjects);
			m_GPUQueryCount = 0;
			m_GPUQueriesReady = false;
		}
	}

	int32 OpenGLContext::BeginGPUPass(const std::string& passName) {
		if (!m_GPUQueriesReady) return -1;
		if (m_ActiveQueryCount >= GPUProfileFrame::kMaxPasses) return -1;

		uint32 nextSlot = m_ActiveQueryCount;
		uint32 qBegin = nextSlot * 2;
		uint32 qEnd   = nextSlot * 2 + 1;

		if (qEnd >= m_GPUQueryCount) return -1;

		auto& query = m_ActiveQueries[nextSlot];
		query.passName      = passName;
		query.queryIndex[0] = m_GPUQueryObjects[qBegin];
		query.queryIndex[1] = m_GPUQueryObjects[qEnd];
		query.active        = true;

		m_GL.QueryCounter(query.queryIndex[0], GL_TIMESTAMP);

		m_ActiveQueryCount = nextSlot + 1;
		return static_cast<int32>(nextSlot);
	}

	void OpenGLContext::EndGPUPass(int32 queryIndex) {
		if (!m_GPUQueriesReady) return;
		if (queryIndex < 0 || queryIndex >= static_cast<int32>(m_ActiveQueryCount))
			return;

		auto& query = m_ActiveQueries[queryIndex];
		if (!query.active) return;

		m_GL.QueryCounter(query.queryIndex[1], GL_TIMESTAMP);
	}

	bool OpenGLContext::GetGPUProfileFrame(GPUProfileFrame& outFrame) {
		outFrame = m_LastGPUProfile;
		return m_LastGPUProfile.passCount > 0;
	}

} // namespace Engine