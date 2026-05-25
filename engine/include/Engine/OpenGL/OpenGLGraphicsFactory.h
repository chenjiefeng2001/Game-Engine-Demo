#pragma once

#include "Engine/Core/IGraphicsFactory.h"
#include <glad/gl.h>

namespace Engine {

	class OpenGLGraphicsFactory : public IGraphicsFactory
	{
	public:
		OpenGLGraphicsFactory();
		virtual ~OpenGLGraphicsFactory() override;

		// ---- COntext with Window ----
		virtual std::unique_ptr<IWindow> CreateWindow(
			int width,
			int height,
			const std::string& title) override;

		virtual std::unique_ptr<IRenderContext> CreateRenderContext(
			void* nativeWindowHandle) override;

		// ---- GPU Resource ----
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

		// ---- Advanced Render Toolbox----
		virtual std::shared_ptr<ISpriteBatch> CreateSpriteBatch(
			IRenderContext& renderContext) override;


		// ---- 获取内部 OpenGL 上下文（仅限 OpenGL 实现内部使用） ----
		GladGLContext& GetGLContext() { return m_GL; }

	private:
		GladGLContext m_GL;
	};

} // namespace Engine
