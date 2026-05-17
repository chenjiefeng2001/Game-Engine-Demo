#pragma once

#include "Engine/Core/RenderResources/Shader.h"
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

	private:
		std::string ReadFile(const std::string& filepath);
		uint32_t CompileShader(unsigned int type, const std::string& source);

	private:
		GladGLContext& m_GL;
		GLuint m_RendererID = 0;
	};

} // namespace Engine
