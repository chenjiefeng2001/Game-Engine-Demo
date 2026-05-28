#pragma once

#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

	/**
	 * @brief 正交相机
	 *
	 * RHI 原则：头文件只依赖 RHI/MathTypes.h（纯数据），不依赖 glm。
	 * 所有数学运算在 .cpp 中使用 glm 实现。
	 */
	class OrthographicCamera {
	public:
		OrthographicCamera() : OrthographicCamera(-1.6f, 1.6f, -0.9f, 0.9f) {}
		OrthographicCamera(float32 left, float32 right, float32 bottom, float32 top);

		/** RHI 接口：通过 float* 返回矩阵，Shader.h 就能直接用 */
		const float32* GetViewProjectionMatrixPtr() const;

	private:
		void RecalculateViewMatrix();

		Mat4 m_ProjectionMatrix;
		Mat4 m_ViewMatrix;
		Mat4 m_ViewProjectionMatrix;
		Vec3 m_Position = { 0.0f, 0.0f, 0.0f };
	};

}