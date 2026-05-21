#pragma once
#include <glm/glm.hpp>

namespace Engine {
	class OrthographicCamera {
	public:
		OrthographicCamera(float left, float right, float bottom, float top);

		// RHI 接口：通过指针返回矩阵，Shader.h 就能直接用
		const float* GetViewProjectionMatrixPtr() const;

	private:
		void RecalculateViewMatrix();

		glm::mat4 m_ProjectionMatrix;
		glm::mat4 m_ViewMatrix;
		glm::mat4 m_ViewProjectionMatrix;
		glm::vec3 m_Position = { 0.0f, 0.0f, 0.0f };
	};
}