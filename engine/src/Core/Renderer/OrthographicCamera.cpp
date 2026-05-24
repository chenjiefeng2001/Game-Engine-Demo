#include "Engine/Core/Renderer/OrthographicCamera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace Engine {

	// ── 辅助转换 ──
	namespace {
		inline glm::mat4 ToGlm(const Mat4& m) {
			glm::mat4 result;
			std::memcpy(&result, m.data, sizeof(float32) * 16);
			return result;
		}

		inline void StoreGlm(const glm::mat4& src, Mat4& dst) {
			std::memcpy(dst.data, &src, sizeof(float32) * 16);
		}
	} // anonymous namespace

	OrthographicCamera::OrthographicCamera(float32 left, float32 right, float32 bottom, float32 top) {
		glm::mat4 proj = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
		StoreGlm(proj, m_ProjectionMatrix);

		glm::mat4 view = glm::mat4(1.0f);
		StoreGlm(view, m_ViewMatrix);

		// m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix
		glm::mat4 vp = ToGlm(m_ProjectionMatrix) * ToGlm(m_ViewMatrix);
		StoreGlm(vp, m_ViewProjectionMatrix);
	}

	const float32* OrthographicCamera::GetViewProjectionMatrixPtr() const {
		return m_ViewProjectionMatrix.Data();
	}

	void OrthographicCamera::RecalculateViewMatrix() {
		glm::mat4 transform = glm::translate(glm::mat4(1.0f),
			glm::vec3(m_Position.x, m_Position.y, m_Position.z));
		glm::mat4 view = glm::inverse(transform);
		StoreGlm(view, m_ViewMatrix);

		glm::mat4 vp = ToGlm(m_ProjectionMatrix) * view;
		StoreGlm(vp, m_ViewProjectionMatrix);
	}
}