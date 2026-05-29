#include "OpenGLTexture.h"

#include "Engine/Core/Log.h"
#include "stb_image.h"

namespace {
    Engine::Logger s_Log("OpenGLTexture");
}

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
			s_Log.Info("Successfully loaded texture: {} ({}x{})", path, m_Width, m_Height);
		} else {
			SetState(ResourceState::Failed);
			s_Log.Error("Failed to load texture: {}", path);
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
			s_Log.Info("[HotReload] Texture reloaded: {} ({}x{})", path, m_Width, m_Height);
			return true;
		} else {
			// 加载失败——恢复旧纹理
			if (oldID) {
				m_RendererID = oldID;
				m_Width = oldWidth;
				m_Height = oldHeight;
			}
			SetState(ResourceState::Loaded);  // 保持可用状态
			s_Log.Error("[HotReload] Failed to reload texture: {}", path);
			return false;
		}
	}

	// ============================================================
	// 加载后初始化钩子：验证 GPU 纹理已创建
	// ============================================================

	bool OpenGLTexture::PostLoad(IGraphicsFactory* factory) {
		(void)factory;
		// 构造函数已负责从文件加载并上传到 GPU，PostLoad 只需要验证状态
		return m_RendererID != 0 && m_Width > 0 && m_Height > 0;
	}

}
