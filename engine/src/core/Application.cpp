#include "Engine/Application.h"
#include "Engine/Platform/PlatformUtils.h"
#include "Engine/Core/Renderer/Renderer.h"
#include "Engine/Core/Renderer/OrthographicCamera.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/IWindow.h"
#include <iostream>

namespace Engine {
	Application::Application() {
		m_Window = IWindow::Create(800, 600, "Final Fix");
		m_Camera = std::make_unique<OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);

		auto context = m_Window->GetContext();

		// ── 资源初始化 ──────────────────────────────────

		// 1. 着色器
		m_Shader = context->CreateShader("assets/shaders/vertex.glsl", "assets/shaders/fragment.glsl");

		// 2. 纹理
		m_Texture = context->CreateTexture("assets/textures/test.png");

		// 3. 顶点数据 — 一个矩形（position.xy, texcoord.uv）
		float vertices[] = {
			// 位置 (x, y, z)    纹理坐标 (u, v)
			-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  // 左下
			 0.5f, -0.5f, 0.0f,  1.0f, 0.0f,  // 右下
			 0.5f,  0.5f, 0.0f,  1.0f, 1.0f,  // 右上
			-0.5f,  0.5f, 0.0f,  0.0f, 1.0f   // 左上
		};
		uint32_t indices[] = {
			0, 1, 2,  // 第一个三角形
			2, 3, 0   // 第二个三角形
		};

		auto vb = context->CreateVertexBuffer(vertices, sizeof(vertices));
		auto ib = context->CreateIndexBuffer(indices, sizeof(indices) / sizeof(uint32_t));

		m_VAO = context->CreateVertexArray();
		m_VAO->AddVertexBuffer(vb);
		m_VAO->SetIndexBuffer(ib);
	}

	Application::~Application() = default;

	void Application::Run() {
		while (!m_Window->ShouldClose()) {
			float time = Time::GetTime();
			float timestep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			auto context = m_Window->GetContext();

			// 背景色清屏
			context->ClearColor(0.2f, 0.2f, 0.2f, 1.0f);

			if (m_Shader && m_VAO && m_Texture) {
				// 1. 绑定着色器并上传摄像机矩阵
				m_Shader->Bind();
				m_Shader->SetMat4("u_ViewProjection", m_Camera->GetViewProjectionMatrixPtr());

				// 2. 绑定贴图 (因为 Shader 里的 sampler2D 默认是 0 号槽位)
				m_Texture->Bind(0);

				// 3. 绑定几何数据
				m_VAO->Bind();

				// 4. ⚠️ 终极核心：发出绘制指令！
				context->DrawIndexed(m_VAO);
			}

			m_Window->OnUpdate();
		}
	}
}