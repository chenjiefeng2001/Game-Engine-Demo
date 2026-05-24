#include "OpenGLShader.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <glad/gl.h>

namespace Engine {

	OpenGLShader::OpenGLShader(const std::string& vertexPath, const std::string& fragmentPath, GladGLContext& gl)
		: m_GL(gl) {

		std::string vSource = ReadFile(vertexPath);
		std::string fSource = ReadFile(fragmentPath);

		uint32 vs = CompileShader(GL_VERTEX_SHADER, vSource);
		uint32 fs = CompileShader(GL_FRAGMENT_SHADER, fSource);

		m_RendererID = m_GL.CreateProgram();
		m_GL.AttachShader(m_RendererID, vs);
		m_GL.AttachShader(m_RendererID, fs);
		m_GL.LinkProgram(m_RendererID);

		int success;
		m_GL.GetProgramiv(m_RendererID, GL_LINK_STATUS, &success);
		if (!success) {
			char infoLog[512];
			m_GL.GetProgramInfoLog(m_RendererID, 512, NULL, infoLog);
			std::cerr << "Shader Linking Error:\n" << infoLog << std::endl;
		}

		m_GL.DeleteShader(vs);
		m_GL.DeleteShader(fs);
	}

	OpenGLShader::~OpenGLShader() {
		m_GL.DeleteProgram(m_RendererID);
	}

	void OpenGLShader::Bind() const { m_GL.UseProgram(m_RendererID); }
	void OpenGLShader::Unbind() const { m_GL.UseProgram(0); }

	std::string OpenGLShader::ReadFile(const std::string& filepath) {
		std::ifstream f(filepath);
		if (!f.is_open()) {
			std::cerr << "Could not open shader file: " << filepath << std::endl;
			return "";
		}
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}

	uint32 OpenGLShader::CompileShader(unsigned int type, const std::string& source) {
		uint32 id = m_GL.CreateShader(type);
		const char* src = source.c_str();
		m_GL.ShaderSource(id, 1, &src, nullptr);
		m_GL.CompileShader(id);

		int result;
		m_GL.GetShaderiv(id, GL_COMPILE_STATUS, &result);
		if (!result) {
			char infoLog[512];
			m_GL.GetShaderInfoLog(id, 512, NULL, infoLog);
			std::cerr << "Shader Compilation Error (" << (type == GL_VERTEX_SHADER ? "vertex" : "fragment") << "):\n" << infoLog << std::endl;
			m_GL.DeleteShader(id);
			return 0;
		}
		return id;
	}
	void OpenGLShader::SetMat4(const std::string& name, const float* data) {
		GLint location = m_GL.GetUniformLocation(m_RendererID, name.c_str());
		m_GL.UniformMatrix4fv(location, 1, GL_FALSE, data);
	}
}
