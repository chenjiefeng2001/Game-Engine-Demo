#pragma once

#include "Engine/Core/IGraphicsFactory.h"
#include <glad/gl.h>

namespace Engine {

	// ============================================================================
	// OpenGLGraphicsFactory - OpenGL 具体工厂
	// 使用 GLFW 创建窗口，使用 OpenGL 4.6 创建所有 GPU 资源
	// ============================================================================
	class OpenGLGraphicsFactory : public IGraphicsFactory
	{
	public:
		OpenGLGraphicsFactory();
		virtual ~OpenGLGraphicsFactory() override;

		// ---- 窗口与上下文 ----
		virtual std::unique_ptr<IWindow> CreateWindow(
			int width,
			int height,
			const std::string& title) override;

		virtual std::unique_ptr<IRenderContext> CreateRenderContext(
			void* nativeWindowHandle) override;

		// ---- GPU 资源 ----
		virtual std::shared_ptr<Shader> CreateShader(
			const std::string& vertexPath,
			const std::string& fragmentPath) override;

		virtual std::shared_ptr<Texture> CreateTexture(
			const std::string& path) override;

		virtual std::shared_ptr<VertexBuffer> CreateVertexBuffer(
			float* vertices,
			uint32_t size) override;

		virtual std::shared_ptr<IndexBuffer> CreateIndexBuffer(
			uint32_t* indices,
			uint32_t count) override;

		virtual std::shared_ptr<VertexArray> CreateVertexArray() override;

		// 获取共享的 OpenGL 上下文引用
		GladGLContext& GetGLContext() { return m_GL; }

	private:
		GladGLContext m_GL;
	};

} // namespace Engine
