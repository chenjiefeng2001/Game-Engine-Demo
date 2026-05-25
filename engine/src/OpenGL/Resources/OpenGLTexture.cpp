#include "OpenGLTexture.h"

#include "stb_image.h"
#include <iostream>

namespace Engine {

	OpenGLTexture::OpenGLTexture(const std::string& path, GladGLContext& gl)
		: Texture(path), m_GL(gl) {

		stbi_set_flip_vertically_on_load(true);

		int width, height, channels;
		unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

		if (data) {
			m_Width = static_cast<uint32>(width);
			m_Height = static_cast<uint32>(height);

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

			SetState(ResourceState::Loaded);
			std::cout << "Successfully loaded texture: " << path << " (" << m_Width << "x" << m_Height << ")" << std::endl;
		} else {
			SetState(ResourceState::Failed);
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

	// ============================================================
	// 热加载：释放旧纹理，重新从磁盘加载
	// ============================================================

	bool OpenGLTexture::Reload() {
		// 保存旧纹理 ID，即使加载失败也要恢复旧纹理
		GLuint oldID = m_RendererID;
		uint32 oldWidth = m_Width;
		uint32 oldHeight = m_Height;

		m_RendererID = 0;
		m_Width = 0;
		m_Height = 0;

		stbi_set_flip_vertically_on_load(true);

		int width, height, channels;
		const std::string& path = GetPath();
		unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

		if (data) {
			m_Width = static_cast<uint32>(width);
			m_Height = static_cast<uint32>(height);

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

			// 释放旧纹理
			if (oldID) {
				m_GL.DeleteTextures(1, &oldID);
			}

			SetState(ResourceState::Loaded);
			std::cout << "[HotReload] Texture reloaded: " << path
					  << " (" << m_Width << "x" << m_Height << ")" << std::endl;
			return true;
		} else {
			// 加载失败——恢复旧纹理
			if (oldID) {
				m_RendererID = oldID;
				m_Width = oldWidth;
				m_Height = oldHeight;
			}
			SetState(ResourceState::Loaded);  // 保持可用状态
			std::cerr << "[HotReload] Failed to reload texture: " << path << std::endl;
			return false;
		}
	}

}
