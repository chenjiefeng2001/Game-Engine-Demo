#pragma once
#include <string>

namespace Engine {
	class Shader {
	public:
		virtual ~Shader() = default;
		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		// ⚠️ RHI 原则：使用 float* 传递矩阵，彻底隔离第三方库
		virtual void SetMat4(const std::string& name, const float* data) = 0;
	};
}