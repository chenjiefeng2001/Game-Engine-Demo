#include "Engine/OpenGL/Resources/OpenGLIndexBuffer.h"

namespace Engine {

	OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t* indices, uint32_t count, GladGLContext& gl)
		: m_GL(gl), m_Count(count)
	{
		m_GL.GenBuffers(1, &m_RendererID);
		m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
		m_GL.BufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
	}

	OpenGLIndexBuffer::~OpenGLIndexBuffer() {
		m_GL.DeleteBuffers(1, &m_RendererID);
	}

	void OpenGLIndexBuffer::Bind() const {
		m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
	}

	void OpenGLIndexBuffer::Unbind() const {
		m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

} 
