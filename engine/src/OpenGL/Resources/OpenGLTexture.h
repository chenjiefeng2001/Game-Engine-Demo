#pragma once

#include "Engine/Core/RenderResources/Texture.h"
#include <glad/gl.h>

namespace Engine {

	class OpenGLTexture : public Texture
	{
	public:
		OpenGLTexture(const std::string& path, GladGLContext& gl);
		virtual ~OpenGLTexture() override;

		virtual void Bind(uint32_t slot = 0) const override;
		virtual uint32_t GetWidth() const override { return m_Width; }
		virtual uint32_t GetHeight() const override { return m_Height; }

	private:
		GladGLContext& m_GL;
		std::string m_Path;
		uint32_t m_Width = 0, m_Height = 0;
		GLuint m_RendererID = 0;
	};

} // namespace Engine
