#include "Engine/OpenGL/Resources/OpenGLVertexBuffer.h"

namespace Engine {

	OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, uint32_t size, GladGLContext& gl)
		: m_GL(gl) {
		m_GL.GenBuffers(1, &m_RendererID);
		m_GL.BindBuffer(GL_ARRAY_BUFFER, m_RendererID);
		m_GL.BufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
	}

	OpenGLVertexBuffer::~OpenGLVertexBuffer() {
		m_GL.DeleteBuffers(1, &m_RendererID);
	}

	void OpenGLVertexBuffer::Bind() const {
		m_GL.BindBuffer(GL_ARRAY_BUFFER, m_RendererID);
	}

	void OpenGLVertexBuffer::Unbind() const {
		m_GL.BindBuffer(GL_ARRAY_BUFFER, 0);
	}

} 
