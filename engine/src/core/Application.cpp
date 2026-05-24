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
#include <iostream>

namespace Engine {
	Application::Application(IGraphicsFactory& factory)
		: m_Factory(factory)
		, m_TextureManager(factory)
	{
		m_Window = m_Factory.CreateWindow(800, 600, "Final Fix");  
		m_Camera = std::make_unique<OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);
		float vertices[] = {
			-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
			 0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
			 0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
			-0.5f,  0.5f, 0.0f,  0.0f, 1.0f
		};
		uint32_t indices[] = {
			0, 1, 2,
			2, 3, 0
		};

		// ── 所有资源通过 m_Factory 创建 ──
		m_Shader = m_Factory.CreateShader("assets/shaders/vertex.glsl", "assets/shaders/fragment.glsl");
		m_Texture = m_TextureManager.Load("assets/textures/test.png");

		auto vb = m_Factory.CreateVertexBuffer(vertices, sizeof(vertices));
		auto ib = m_Factory.CreateIndexBuffer(indices, sizeof(indices) / sizeof(uint32_t));

		m_VAO = m_Factory.CreateVertexArray();
		m_VAO->AddVertexBuffer(vb);
		m_VAO->SetIndexBuffer(ib);
	}

	Application::~Application() = default;
	void Application::Update(float dt) {
		if (Input::IsKeyDown(KeyCode::W)) {
			 m_Camera->MoveForward(dt);
		}
		if (Input::IsKeyDown(KeyCode::A)) {
			 m_Camera->MoveLeft(dt);
		}

		if (Input::IsKeyPressed(KeyCode::Space)) {
			 m_Player->Jump();
		}

		float dx = Input::GetMouseDeltaX();
		float dy = Input::GetMouseDeltaY();
		 m_Camera->Rotate(dx, dy);
	}

	void Application::Render() {
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
	}
	void Application::Run() {
		m_LastFrameTime = Time::GetTime();

		while (!m_Window->ShouldClose()) {
			float time = Time::GetTime();
			float dt = time - m_LastFrameTime;
			m_LastFrameTime = time;

			if (dt > 0.25f) dt = 0.25f;

			if (m_LoopMode == LoopMode::Variable) {

				Update(dt);
				Render();

			}
			else {
				// ── 固定步长模式 (Fixed 60Hz) ──
				static const float fixedDt = 1.0f / 60.0f;
				static float accumulator = 0.0f;

				accumulator += dt;

				// 用 while 循环追赶固定步长
				while (accumulator >= fixedDt) {
					Update(fixedDt);        
					accumulator -= fixedDt;
				}

				Render();                 
			}

			m_Window->OnUpdate();
		}
	}
	}