#pragma once

#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Types.h"
#include <glad/gl.h>

namespace Engine {

	class OpenGLTexture : public Texture
	{
	public:
		OpenGLTexture(const std::string& path, GladGLContext& gl);
		virtual ~OpenGLTexture() override;

		virtual void Bind(uint32 slot = 0) const override;
		virtual uint32 GetWidth() const override { return m_Width; }
		virtual uint32 GetHeight() const override { return m_Height; }

	private:
		GladGLContext& m_GL;
		std::string m_Path;
		uint32 m_Width = 0, m_Height = 0;
		GLuint m_RendererID = 0;
	};

} // namespace Engine
