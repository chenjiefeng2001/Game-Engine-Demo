#include "OpenGLTexture.h"

#include <GLFW/glfw3.h>

#include "Engine/Core/Log.h"
#include "stb_image.h"

namespace {
    Engine::Logger s_Log("OpenGLTexture");

    /// glad 的函数指针全为 null 表示「从未创建 GL 上下文」（gladLoadGLContext 未跑过）。
    /// 此时调用任何 GL 入口都是跳转到地址 0 —— 在 Linux 上表现为 SIGSEGV 且 rip=0x0。
    ///
    /// 这条路径此前从未在 Linux CI 上执行到：场景 fixture 缺失导致项目打不开，
    /// project-open 里的纹理上传根本不会发生；fixture 修好后它立刻暴露。
    static bool HasGLEntryPoints(const GladGLContext& gl) {
        return gl.CreateTextures && gl.TextureStorage2D &&
               gl.TextureSubImage2D && gl.GenerateTextureMipmap;
    }
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

			// 选择「明确失败」而不是「假装加载成功」：磁盘上有文件、尺寸已知，但 GPU
			// 上并没有对应资源，标成 Loaded 会让真实渲染里出现「贴图全黑却无任何报错」。
			// m_RendererID 保持 0，析构里的 DeleteTextures 守卫也因此不会碰空指针。
			if (!HasGLEntryPoints(m_GL)) {
				stbi_image_free(data);
				SetState(ResourceState::Failed);
				s_Log.Error("Failed to load texture {}: no OpenGL context (GL entry points not loaded)",
					path);
				return;
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
		if (m_RendererID && glfwGetCurrentContext()) {
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

			// 与构造函数同一个守卫：reload 路径同样可能在无上下文时被走到。
			if (!HasGLEntryPoints(m_GL)) {
				stbi_image_free(data);
				SetState(ResourceState::Failed);
				s_Log.Error("Failed to reload texture {}: no OpenGL context (GL entry points not loaded)",
					path);
				return false;
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
