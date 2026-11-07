#include "OpenGLIndexBuffer.h"
#include <atomic>
#include <cstdint>

namespace Engine {

	namespace { std::atomic<int64_t> g_Live{0}; }

	OpenGLIndexBuffer::OpenGLIndexBuffer(uint32* indices, uint32 count, GladGLContext& gl)
		: m_GL(gl), m_Count(count)
	{
		++g_Live;
		m_GL.GenBuffers(1, &m_RendererID);
		m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
		m_GL.BufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32), indices, GL_STATIC_DRAW);
	}

	OpenGLIndexBuffer::~OpenGLIndexBuffer() {
		--g_Live;
		m_GL.DeleteBuffers(1, &m_RendererID);
	}

	int64_t OpenGLIndexBuffer::LiveCount() { return g_Live.load(); }

	void OpenGLIndexBuffer::Bind() const {
		m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
	}

	void OpenGLIndexBuffer::Unbind() const {
		m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

}
