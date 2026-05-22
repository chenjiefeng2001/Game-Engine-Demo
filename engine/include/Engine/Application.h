#pragma once

#include "Engine/Core/IWindow.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/Renderer/OrthographicCamera.h"
#include <memory>

namespace Engine {

	class IWindow;
	class Shader;
	class VertexArray;
	class Texture;
	class IGraphicsFactory;
	class OrthographicCamera;

	// ============================================================================
	// Application - 应用程序主类
	// 通过 IGraphicsFactory 创建窗口和渲染资源，不直接依赖具体实现
	// ============================================================================
	class Application
	{
		public:
			//Dependency Inject
		Application(IGraphicsFactory& factory);
		~Application();
		void Run();
	private:
		IGraphicsFactory& m_Factory;
		std::unique_ptr<class IWindow> m_Window;
		std::shared_ptr<class Shader> m_Shader;
		std::shared_ptr<class VertexArray> m_VAO;
		std::shared_ptr<class Texture> m_Texture;
		std::unique_ptr<class OrthographicCamera> m_Camera;
		float m_LastFrameTime = 0.0f;

	};

} // namespace Engine