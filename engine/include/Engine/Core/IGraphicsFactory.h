#pragma once

#include <memory>
#include <string>
#include "Engine/Types.h"

namespace Engine {

	// 前向声明
	class IWindow;
	class IRenderContext;
	class Shader;
	class Texture;
	class VertexBuffer;
	class IndexBuffer;
	class VertexArray;
	class ISpriteBatch;
	class IUIManager;
	class StackAllocator;

// ============================================================
// RHI 抽象工厂 — 完全与具体图形 API 解耦
// ============================================================
// 不包含任何 OpenGL / Vulkan / DirectX 等具体 API 头文件。
// 具体实现类（如 OpenGLGraphicsFactory）负责管理自己的 API 上下文。
	class IGraphicsFactory
	{
	public:
		virtual ~IGraphicsFactory() = default;

		// ---- 分配器支持 ----
		/** 设置栈分配器，工厂将使用它分配所有子系统对象 */
		void SetAllocator(StackAllocator* alloc) noexcept { m_Allocator = alloc; }
		/** 获取当前的栈分配器 */
		StackAllocator* GetAllocator() const noexcept { return m_Allocator; }

		// ---- 窗口与上下文 ----
		virtual std::unique_ptr<IWindow> CreateWindow(
			int width, int height, const std::string& title) = 0;

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
			uint32 size) = 0;

		virtual std::shared_ptr<IndexBuffer> CreateIndexBuffer(
			uint32* indices,
			uint32 count) = 0;

		virtual std::shared_ptr<VertexArray> CreateVertexArray() = 0;

		// ---- 高级渲染工具 ----
		virtual std::shared_ptr<ISpriteBatch> CreateSpriteBatch(
			IRenderContext& renderContext) = 0;

		// ---- UI 管理器 ----
		virtual std::unique_ptr<IUIManager> CreateUIManager() = 0;

	protected:
		StackAllocator* m_Allocator = nullptr;
	};

}
