#include "OpenGLTexture.h"

#include "stb_image.h"
#include <iostream>

namespace Engine {

	OpenGLTexture::OpenGLTexture(const std::string& path, GladGLContext& gl)
		: m_GL(gl), m_Path(path) {

		stbi_set_flip_vertically_on_load(true);

		int width, height, channels;
		unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

		if (data) {
			m_Width = static_cast<uint32_t>(width);
			m_Height = static_cast<uint32_t>(height);

			GLenum internalFormat = 0, dataFormat = 0;
			if (channels == 4) {
				internalFormat = GL_RGBA8;
				dataFormat = GL_RGBA;
			} else if (channels == 3) {
				internalFormat = GL_RGB8;
				dataFormat = GL_RGB;
			}

			m_GL.CreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
			m_GL.TextureStorage2D(m_RendererID, 1, internalFormat, m_Width, m_Height);

			m_GL.TextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			m_GL.TextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			m_GL.TextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
			m_GL.TextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

			m_GL.TextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, dataFormat, GL_UNSIGNED_BYTE, data);
			m_GL.GenerateTextureMipmap(m_RendererID);

			stbi_image_free(data);

			std::cout << "Successfully loaded texture: " << path << " (" << m_Width << "x" << m_Height << ")" << std::endl;
		} else {
			std::cerr << "Failed to load texture: " << path << std::endl;
		}
	}

	OpenGLTexture::~OpenGLTexture() {
		if (m_RendererID) {
			m_GL.DeleteTextures(1, &m_RendererID);
		}
	}

	void OpenGLTexture::Bind(uint32_t slot) const {
		m_GL.BindTextureUnit(slot, m_RendererID);
	}

}
