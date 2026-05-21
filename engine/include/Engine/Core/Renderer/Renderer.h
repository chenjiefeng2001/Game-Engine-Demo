#pragma once
#include <memory>
//abstract declaim not include impl
namespace Engine {
	class OrthographicCamera;
	class Shader;
	class VertexArray;

	class Renderer {
	public:
		static void BeginScene(OrthographicCamera& camera);
		static void EndScene();
		static void Submit(const std::shared_ptr<Shader>& shader, const std::shared_ptr<VertexArray>& va);
	};
}