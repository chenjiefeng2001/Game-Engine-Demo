#pragma once

#include "Engine/Core/IWindow.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/Renderer/OrthographicCamera.h"
#include <memory>

namespace Engine {

	class IWindow;
	class Shader;
	class VertexArray;
	class Texture;
	class TextureManager;
	class IGraphicsFactory;
	class OrthographicCamera;


	enum class LoopMode {
		Variable,       // 可变步长：每帧 dt 取决于实际时间
		Fixed           // 固定步长：物理/逻辑固定 60Hz，渲染按实际帧率
	};
	class Application
	{
		public:
		//Dependency Inject
		Application(IGraphicsFactory& factory);
		~Application();
		void Run();

		TextureManager& GetTextureManager() { return m_TextureManager; }

	private:
		IGraphicsFactory& m_Factory;
		TextureManager    m_TextureManager;
		std::unique_ptr<class IWindow> m_Window;
		std::shared_ptr<class Shader> m_Shader;
		std::shared_ptr<class VertexArray> m_VAO;
		std::shared_ptr<class Texture> m_Texture;
		std::unique_ptr<class OrthographicCamera> m_Camera;
		float m_LastFrameTime = 0.0f;
		// ── 主循环状态 ──
		LoopMode m_LoopMode;

		// ── 内部方法 ──
		void Update(float dt);   
		void Render();           
	};

} // namespace Engine