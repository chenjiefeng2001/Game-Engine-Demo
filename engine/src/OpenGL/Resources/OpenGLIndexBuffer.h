#pragma once

#include "Engine/Core/RenderResources/IndexBuffer.h"
#include <glad/gl.h>

namespace Engine {

	class OpenGLIndexBuffer : public IndexBuffer
	{
	public:
		OpenGLIndexBuffer(uint32_t* indices, uint32_t count, GladGLContext& gl);
		virtual ~OpenGLIndexBuffer() override;

		virtual void Bind() const override;
		virtual void Unbind() const override;
		virtual uint32_t GetCount() const override { return m_Count; }

	private:
		GladGLContext& m_GL;
		uint32_t m_Count = 0;
		GLuint m_RendererID = 0;
	};

} // namespace Engine
