#pragma once

#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Types.h"
#include <glad/gl.h>

namespace Engine {

	class OpenGLVertexBuffer : public VertexBuffer
	{
	public:
		// P6-1B B5-G2: live GL object counter for resource-rollback tests.
		static int64_t LiveCount();
		OpenGLVertexBuffer(float32* vertices, uint32 size, GladGLContext& gl);
		virtual ~OpenGLVertexBuffer() override;

		virtual void Bind() const override;
		virtual void Unbind() const override;

	private:
		GladGLContext& m_GL;
		GLuint m_RendererID = 0;
	};

} // namespace Engine
