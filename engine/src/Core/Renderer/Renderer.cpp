#include "Engine/Core/Renderer/Renderer.h"
#include "Engine/Core/Renderer/OrthographicCamera.h"
// ⚠️ 必须包含这些，否则 shared_ptr<Shader> 无法调用 ->Bind()
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/Texture.h"
#include <cstring>

namespace Engine {
	// 静态变量定义：使用 RHI 纯数据 Mat4，不依赖 glm
	struct SceneData { Mat4 VP; };
	static SceneData s_Data;

	void Renderer::BeginScene(OrthographicCamera& camera) {
		// RHI 原则：通过 float* 指针复制矩阵数据
		const float32* src = camera.GetViewProjectionMatrixPtr();
		std::memcpy(s_Data.VP.Data(), src, sizeof(float32) * 16);
	}

	void Renderer::EndScene() {}

	void Renderer::Submit(const std::shared_ptr<Shader>& shader, const std::shared_ptr<VertexArray>& va) {
		shader->Bind();
		// RHI 原则：传递 float* 指针
		shader->SetMat4("u_ViewProjection", s_Data.VP.Data());
		va->Bind();
		// 这里最终调用底层的绘制，暂时留空或调用 Context
	}
}