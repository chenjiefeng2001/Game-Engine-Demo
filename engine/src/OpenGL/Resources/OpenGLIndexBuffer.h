#pragma once

#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Types.h"
#include <glad/gl.h>

namespace Engine {

	class OpenGLIndexBuffer : public IndexBuffer
	{
	public:
		// P6-1B B5-G2: live GL object counter for resource-rollback tests.
		static int64_t LiveCount();
		OpenGLIndexBuffer(uint32* indices, uint32 count, GladGLContext& gl);
		virtual ~OpenGLIndexBuffer() override;

		virtual void Bind() const override;
		virtual void Unbind() const override;
		virtual uint32 GetCount() const override { return m_Count; }

	private:
		GladGLContext& m_GL;
		uint32 m_Count = 0;
		GLuint m_RendererID = 0;
	};

} // namespace Engine
