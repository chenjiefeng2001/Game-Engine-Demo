#pragma once

#include "Engine/Core/IWindow.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/IGraphicsFactory.h"

#include <memory>

namespace Engine {

	class IWindow;
	class Shader;
	class VertexArray;
	class Texture;
	class IGraphicsFactory;

	// ============================================================================
	// Application - 应用程序主类
	// 通过 IGraphicsFactory 创建窗口和渲染资源，不直接依赖具体实现
	// ============================================================================
	class Application
	{
	public:
		Application();
		~Application() = default;

		void Run();

	private:
		std::unique_ptr<IGraphicsFactory> m_Factory;
		std::unique_ptr<IWindow> m_Window;
		std::shared_ptr<Shader> m_Shader;
		std::shared_ptr<VertexArray> m_VAO;
		std::shared_ptr<Texture> m_Texture;
	};

} // namespace Engine