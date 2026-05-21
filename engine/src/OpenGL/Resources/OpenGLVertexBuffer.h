#pragma once

#include "Engine/Core/RenderResources/VertexBuffer.h"
#include <glad/gl.h>

namespace Engine {

	class OpenGLVertexBuffer : public VertexBuffer
	{
	public:
		OpenGLVertexBuffer(float* vertices, uint32_t size, GladGLContext& gl);
		virtual ~OpenGLVertexBuffer() override;

		virtual void Bind() const override;
		virtual void Unbind() const override;

	private:
		GladGLContext& m_GL;
		GLuint m_RendererID = 0;
	};

} // namespace Engine
