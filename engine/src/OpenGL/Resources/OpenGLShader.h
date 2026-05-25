#pragma once

#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Types.h"
#include <glad/gl.h>
#include <string>

namespace Engine {

	class OpenGLShader : public Shader
	{
	public:
		OpenGLShader(const std::string& vertexPath, const std::string& fragmentPath, GladGLContext& gl);
		virtual ~OpenGLShader() override;

		virtual void Bind() const override;
		virtual void Unbind() const override;
		virtual void SetMat4(const std::string& name, const float32* data) override;

		// ── 热加载 ──
		bool Reload() override;

	private:
		std::string ReadFile(const std::string& filepath);
		uint32 CompileShader(unsigned int type, const std::string& source);
		bool LinkProgram();

		GladGLContext& m_GL;
		GLuint m_RendererID = 0;

		// 保存路径以便热加载
		std::string m_VertexPath;
		std::string m_FragmentPath;
	};

} // namespace Engine
