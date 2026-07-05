#include "OpenGLVertexArray.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"

namespace Engine {

	OpenGLVertexArray::OpenGLVertexArray(GladGLContext& gl)
		: m_GL(gl) {
		m_GL.GenVertexArrays(1, &m_RendererID);
	}

	OpenGLVertexArray::~OpenGLVertexArray() {
		m_GL.DeleteVertexArrays(1, &m_RendererID);
	}

	void OpenGLVertexArray::Bind() const {
		m_GL.BindVertexArray(m_RendererID);
	}

	void OpenGLVertexArray::Unbind() const {
		m_GL.BindVertexArray(0);
	}

	void OpenGLVertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vb) {
		m_GL.BindVertexArray(m_RendererID);
		vb->Bind();

		m_GL.EnableVertexAttribArray(0);
		m_GL.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

		m_GL.EnableVertexAttribArray(1);
		m_GL.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

		m_VertexBuffers.push_back(vb);
	}

	void OpenGLVertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vb,
	                                         const VertexAttribute* attributes,
	                                         uint32 attributeCount) {
		m_GL.BindVertexArray(m_RendererID);
		vb->Bind();

		for (uint32 i = 0; i < attributeCount; ++i) {
			const auto& attr = attributes[i];
			m_GL.EnableVertexAttribArray(attr.location);
			m_GL.VertexAttribPointer(attr.location, attr.size, GL_FLOAT, GL_FALSE,
			                         attr.stride, reinterpret_cast<void*>(static_cast<uintptr_t>(attr.offset)));
		}

		m_VertexBuffers.push_back(vb);
	}

	void OpenGLVertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& ib) {
		m_GL.BindVertexArray(m_RendererID);
		ib->Bind();

		m_IndexBuffer = ib;
	}

}
