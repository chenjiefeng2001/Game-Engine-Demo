#include "Engine/Application.h"

#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"

#include "Engine/OpenGL/OpenGLGraphicsFactory.h"

#include <iostream>
#include <filesystem>

namespace Engine {

	Application::Application()
	{
		std::cout << "Current Working Directory: " 
				  << std::filesystem::current_path() << std::endl;

		m_Factory = std::make_unique<OpenGLGraphicsFactory>();
		m_Window  = m_Factory->CreateWindow(800, 600, "Advanced Engine Architecture");

		auto context = m_Window->GetContext();

		m_Shader = m_Factory->CreateShader(
			"assets/shaders/vertex.glsl",
			"assets/shaders/fragment.glsl");

		float vertices[] = {
			 0.5f,  0.5f, 0.0f,   1.0f, 1.0f,
			 0.5f, -0.5f, 0.0f,   1.0f, 0.0f,
			-0.5f, -0.5f, 0.0f,   0.0f, 0.0f,
			-0.5f,  0.5f, 0.0f,   0.0f, 1.0f
		};
		auto vbo = m_Factory->CreateVertexBuffer(vertices, sizeof(vertices));

		uint32_t indices[] = {
			0, 1, 3,
			1, 2, 3
		};
		auto ebo = m_Factory->CreateIndexBuffer(indices, 6);

		m_VAO = m_Factory->CreateVertexArray();
		m_VAO->AddVertexBuffer(vbo);
		m_VAO->SetIndexBuffer(ebo);

		m_Texture = m_Factory->CreateTexture("assets/textures/test.png");
	}

	void Application::Run()
	{
		while (!m_Window->ShouldClose())
		{
			auto context = m_Window->GetContext();

			context->ClearColor(0.1f, 0.1f, 0.12f, 1.0f);

			if (m_Shader)  m_Shader->Bind();
			if (m_Texture) m_Texture->Bind(0);

			if (m_VAO)
			{
				context->DrawIndexed(m_VAO);
			}

			m_Window->OnUpdate();
		}
	}

} 

