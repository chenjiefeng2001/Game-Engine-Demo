#include "Engine/Core/Renderer/Renderer.h"
#include "Engine/Core/Renderer/OrthographicCamera.h"
// ⚠️ 必须包含这些，否则 shared_ptr<Shader> 无法调用 ->Bind()
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/Texture.h"

namespace Engine {
	// 静态变量定义
	struct SceneData { glm::mat4 VP; };
	static SceneData s_Data;

	void Renderer::BeginScene(OrthographicCamera& camera) {
		// 使用接口获取矩阵指针
		s_Data.VP = *((glm::mat4*)camera.GetViewProjectionMatrixPtr());
	}

	void Renderer::EndScene() {}

	void Renderer::Submit(const std::shared_ptr<Shader>& shader, const std::shared_ptr<VertexArray>& va) {
		shader->Bind();
		// 传递 VP 矩阵
		shader->SetMat4("u_ViewProjection", &s_Data.VP[0][0]);
		va->Bind();
		// 这里最终调用底层的绘制，暂时留空或调用 Context
	}
}