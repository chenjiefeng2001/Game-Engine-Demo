#include "OpenGLVertexBuffer.h"
#include <atomic>
#include <cstdint>

namespace Engine {

	namespace { std::atomic<int64_t> g_Live{0}; }

	OpenGLVertexBuffer::OpenGLVertexBuffer(float32* vertices, uint32 size, GladGLContext& gl)
		: m_GL(gl) {
		++g_Live;
		m_GL.GenBuffers(1, &m_RendererID);
		m_GL.BindBuffer(GL_ARRAY_BUFFER, m_RendererID);
		m_GL.BufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
	}

	OpenGLVertexBuffer::~OpenGLVertexBuffer() {
		--g_Live;
		m_GL.DeleteBuffers(1, &m_RendererID);
	}

	int64_t OpenGLVertexBuffer::LiveCount() { return g_Live.load(); }

	void OpenGLVertexBuffer::Bind() const {
		m_GL.BindBuffer(GL_ARRAY_BUFFER, m_RendererID);
	}

	void OpenGLVertexBuffer::Unbind() const {
		m_GL.BindBuffer(GL_ARRAY_BUFFER, 0);
	}

}
