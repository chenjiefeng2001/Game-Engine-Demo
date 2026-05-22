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
	Application::Application(IGraphicsFactory& factory)
		: m_Factory(factory)
	{
		m_Window = m_Factory.CreateWindow(800, 600, "Final Fix");  // ← 改这里
		m_Camera = std::make_unique<OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);

		// ── 顶点数据必须先定义，否则下面没法用 ──
		float vertices[] = {
			-0.5f, -0.5f, 0.0f,  0.0f, 0.0f,   // 左下
			 0.5f, -0.5f, 0.0f,  1.0f, 0.0f,   // 右下
			 0.5f,  0.5f, 0.0f,  1.0f, 1.0f,   // 右上
			-0.5f,  0.5f, 0.0f,  0.0f, 1.0f    // 左上
		};
		uint32_t indices[] = {
			0, 1, 2,   // 第一个三角形
			2, 3, 0    // 第二个三角形
		};

		// ── 所有资源通过 m_Factory 创建 ──
		m_Shader = m_Factory.CreateShader("assets/shaders/vertex.glsl", "assets/shaders/fragment.glsl");
		m_Texture = m_Factory.CreateTexture("assets/textures/test.png");

		auto vb = m_Factory.CreateVertexBuffer(vertices, sizeof(vertices));
		auto ib = m_Factory.CreateIndexBuffer(indices, sizeof(indices) / sizeof(uint32_t));

		m_VAO = m_Factory.CreateVertexArray();
		m_VAO->AddVertexBuffer(vb);
		m_VAO->SetIndexBuffer(ib);
	}

	Application::~Application() = default;

    void Application::Run() {
        const float fixedDt = 1.0f / 60.0f;  // 固定物理步长 60Hz
        float accumulator = 0.0f;

        while (!m_Window->ShouldClose()) {
            float time = Time::GetTime();
            float frameTime = time - m_LastFrameTime;
            m_LastFrameTime = time;

            // 防止螺旋式死亡（例如断点调试回来时 frameTime 极大）
            if (frameTime > 0.25f)
                frameTime = 0.25f;

            accumulator += frameTime;

            // ── 固定步长更新（物理/逻辑）──
            while (accumulator >= fixedDt) {
                // Update(fixedDt);     ← 所有物理/逻辑放这里
                // m_Camera->Update(fixedDt);
                // m_Scene->Update(fixedDt);
                accumulator -= fixedDt;
            }

            // ── 渲染（用实际帧率，不受 fixedDt 限制）──
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

            m_Window->OnUpdate();
        }
    }
	}