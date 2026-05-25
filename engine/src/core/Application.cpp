#include "Engine/Application.h"
#include "Engine/Platform/PlatformUtils.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/IWindow.h"
#include "Engine/Core/Resources/ResourceManager.h"
#include "Engine/Core/Resources/FileWatcher.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/UIManager.h"
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <iostream>

namespace Engine {


	Application::Application(IGraphicsFactory& factory)
		: m_Factory(factory)
		, m_TextureManager(factory)
	{

		m_StartupQueue.Add("Window", StartupPhase::Platform,
			[this]() { return InitWindow(); });


		m_StartupQueue.Add("Camera", StartupPhase::Graphics,
			[this]() { return InitCamera(); });


		m_StartupQueue.Add("UI", StartupPhase::Graphics,
			[this]() { return InitUI(); },
			[]() { UIManager::Shutdown(); });

		// ── 统一资源管理器（在窗口和图形上下文就绪后初始化） ──
		m_StartupQueue.Add("ResourceManager", StartupPhase::Resources,
			[this]() {
				ResourceManager::Init(m_Factory);
				return ResourceManager::Get() != nullptr;
			},
			[]() { ResourceManager::Shutdown(); });

		// ── 文件监视器（用于热加载） ──
		m_StartupQueue.Add("FileWatcher", StartupPhase::Resources,
			[]() {
				FileWatcher::Init();
				return FileWatcher::Get() != nullptr;
			},
			[]() { FileWatcher::Shutdown(); });

		m_StartupQueue.Add("Shader", StartupPhase::Resources,
			[this]() { return InitShader(); });


		m_StartupQueue.Add("Texture", StartupPhase::Assets,
			[this]() {
				m_Texture = m_TextureManager.Load("assets/textures/test.png");
				return m_Texture != nullptr;
			},
			[this]() { m_Texture.reset(); });

		m_StartupQueue.Add("VertexData", StartupPhase::Assets,
			[this]() { return InitVertexData(); });
	}

	Application::~Application() {
		// StartupQueue::Shutdown() 在 m_StartupQueue 析构时自动调用
	}


	void Application::RegisterInitStep(std::string name, StartupPhase phase,
	                                   std::function<bool()> init,
	                                   std::function<void()> shutdown) {
		m_StartupQueue.Add(std::move(name), phase,
		                   std::move(init), std::move(shutdown));
	}



	bool Application::InitWindow() {
		m_Window = m_Factory.CreateWindow(800, 600, "Engine");
		if (!m_Window) {
			std::cerr << "[Application] Failed to create window" << std::endl;
			return false;
		}
		std::cout << "[Application] Window created (800x600)" << std::endl;
		return true;
	}

	bool Application::InitCamera() {
		m_Camera = std::make_unique<OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);
		return true;
	}

	bool Application::InitShader() {
		m_Shader = m_Factory.CreateShader(
			"assets/shaders/vertex.glsl",
			"assets/shaders/fragment.glsl"
		);
		if (!m_Shader) {
			std::cerr << "[Application] Failed to create shader" << std::endl;
			return false;
		}
		return true;
	}

	bool Application::InitVertexData() {
		float vertices[] = {
			-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
			 0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
			 0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
			-0.5f,  0.5f, 0.0f,  0.0f, 1.0f
		};
		uint32 indices[] = {
			0, 1, 2,
			2, 3, 0
		};

		auto vb = m_Factory.CreateVertexBuffer(vertices, sizeof(vertices));
		if (!vb) {
			std::cerr << "[Application] Failed to create vertex buffer" << std::endl;
			return false;
		}

		auto ib = m_Factory.CreateIndexBuffer(indices, sizeof(indices) / sizeof(uint32));
		if (!ib) {
			std::cerr << "[Application] Failed to create index buffer" << std::endl;
			return false;
		}

		m_VAO = m_Factory.CreateVertexArray();
		m_VAO->AddVertexBuffer(vb);
		m_VAO->SetIndexBuffer(ib);

		std::cout << "[Application] Vertex data initialized"
		          << " (4 vertices, 6 indices)" << std::endl;
		return true;
	}

	bool Application::InitUI() {
		GLFWwindow* nativeWindow = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
		OpenGLContext* glContext = static_cast<OpenGLContext*>(m_Window->GetContext());
		UIManager::Init(nativeWindow, glContext->GetGL());
		return UIManager::Get() != nullptr && UIManager::Get()->IsInitialized();
	}


	void Application::InternalUpdate(float32 dt) {
		// 默认的引擎级输入处理（可被子类 OnUpdate 扩展）
		if (Input::IsKeyDown(KeyCode::W)) {
			// m_Camera->MoveForward(dt);
		}
		if (Input::IsKeyDown(KeyCode::A)) {
			// m_Camera->MoveLeft(dt);
		}

		float32 dx = Input::GetMouseDeltaX();
		float32 dy = Input::GetMouseDeltaY();
		// m_Camera->Rotate(dx, dy);

		// 子类扩展
		OnUpdate(dt);
	}

	void Application::InternalRender() {
		auto context = m_Window->GetContext();
		context->ClearColor(0.2f, 0.2f, 0.2f, 1.0f);

		if (m_Shader && m_VAO && m_Texture) {
			m_Shader->Bind();
			m_Shader->SetMat4("u_ViewProjection",
				m_Camera->GetViewProjectionMatrixPtr());
			m_Texture->Bind(0);
			m_VAO->Bind();
			context->DrawIndexed(m_VAO);
		}

		// 子类扩展
		OnRender();
	}


	void Application::Run() {

		if (!m_StartupQueue.Execute()) {
			std::cerr << "[Application] Startup failed, aborting." << std::endl;
			return;
		}

		OnStartup();

		// ── 启动文件监视器（在资源加载完成后） ──
		if (auto* fw = FileWatcher::Get())
			fw->Start();

		// ── 3. 主循环 ──
		m_LastFrameTime = Time::GetTime();

		while (!m_Window->ShouldClose()) {
			float32 time = Time::GetTime();
			float32 dt = time - m_LastFrameTime;
			m_LastFrameTime = time;

			if (dt > 0.25f) dt = 0.25f;

			m_Window->PollEvents();

			// ── UI 帧开始（内含 NewFrame + 输入抢占） ──
			if (auto* ui = UIManager::Get())
				ui->Begin();

			if (m_LoopMode == LoopMode::Variable) {
				InternalUpdate(dt);
			} else {
				// 固定步长模式 (Fixed 60Hz)
				static const float32 fixedDt = 1.0f / 60.0f;
				static float32 accumulator = 0.0f;

				accumulator += dt;
				while (accumulator >= fixedDt) {
					InternalUpdate(fixedDt);
					accumulator -= fixedDt;
				}
			}

			// ── 每帧检测文件变更，触发热加载 ──
			if (auto* rm = ResourceManager::Get()) {
				rm->PollHotReload();
				rm->ProcessAsyncLoads();
			}

			// ── 构建 UI（仅在可见时执行） ──
			if (auto* ui = UIManager::Get(); ui && ui->IsVisible())
			{
				// 收集渲染统计（在构建 UI 前，让 DrawCall 计数反映上一帧的数据）
				uint32 drawCalls = m_Window->GetContext()->GetAndResetDrawCallCount();

				m_PerfWindow.FeedStats(
					dt * 1000.0f,         
					drawCalls,              
					0,                      
					0,                       
					0,                      
					0                        
				);

				// 绘制性能监控窗口
				m_PerfWindow.OnImGui();

				// 子类自定义 UI
				OnImGui();
			}

			InternalRender();

			// ── UI 帧结束 + 渲染 ──
			if (auto* ui = UIManager::Get())
				ui->End();

			m_Window->OnUpdate();
		}
	}

} // namespace Engine