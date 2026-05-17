#pragma once

#include <memory>
#include <string>
#include <cstdint>

namespace Engine {

	// 前向声明
	class IWindow;
	class IRenderContext;
	class Shader;
	class Texture;
	class VertexBuffer;
	class IndexBuffer;
	class VertexArray;

//RHI API
	class IGraphicsFactory
	{
	public:
		virtual ~IGraphicsFactory() = default;

		// ---- 窗口与上下文 ----
		virtual std::unique_ptr<IWindow> CreateWindow(
			int width,
			int height,
			const std::string& title) = 0;

		virtual std::unique_ptr<IRenderContext> CreateRenderContext(
			void* nativeWindowHandle) = 0;

		// ---- GPU 资源 ----
		virtual std::shared_ptr<Shader> CreateShader(
			const std::string& vertexPath,
			const std::string& fragmentPath) = 0;

		virtual std::shared_ptr<Texture> CreateTexture(
			const std::string& path) = 0;

		virtual std::shared_ptr<VertexBuffer> CreateVertexBuffer(
			float* vertices,
			uint32_t size) = 0;

		virtual std::shared_ptr<IndexBuffer> CreateIndexBuffer(
			uint32_t* indices,
			uint32_t count) = 0;

		virtual std::shared_ptr<VertexArray> CreateVertexArray() = 0;
	};

} 
