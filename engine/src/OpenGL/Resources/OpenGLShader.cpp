#include "OpenGLShader.h"

#include "Engine/Core/Log.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <glad/gl.h>

namespace {
    Engine::Logger s_Log("OpenGLShader");
}

namespace Engine {

	OpenGLShader::OpenGLShader(const std::string& vertexPath, const std::string& fragmentPath, GladGLContext& gl)
		: Shader(vertexPath + "|" + fragmentPath), m_GL(gl)
		, m_VertexPath(vertexPath), m_FragmentPath(fragmentPath) {

		std::string vSource = ReadFile(vertexPath);
		std::string fSource = ReadFile(fragmentPath);

		uint32 vs = CompileShader(GL_VERTEX_SHADER, vSource);
		uint32 fs = CompileShader(GL_FRAGMENT_SHADER, fSource);

		m_RendererID = m_GL.CreateProgram();
		m_GL.AttachShader(m_RendererID, vs);
		m_GL.AttachShader(m_RendererID, fs);

		if (!LinkProgram()) {
			SetState(ResourceState::Failed);
		} else {
			SetState(ResourceState::Loaded);
		}

		m_GL.DeleteShader(vs);
		m_GL.DeleteShader(fs);

	}

	OpenGLShader::~OpenGLShader() {
		if (m_RendererID) {
			m_GL.DeleteProgram(m_RendererID);
		}
	}

	void OpenGLShader::Bind() const { m_GL.UseProgram(m_RendererID); }
	void OpenGLShader::Unbind() const { m_GL.UseProgram(0); }

	std::string OpenGLShader::ReadFile(const std::string& filepath) {
		std::ifstream f(filepath);
		if (!f.is_open()) {
			s_Log.Error("Could not open shader file: {}", filepath);
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
			s_Log.Error("Shader Compilation Error ({}):\n{}", (type == GL_VERTEX_SHADER ? "vertex" : "fragment"), infoLog);
			m_GL.DeleteShader(id);
			return 0;
		}
		return id;
	}

	bool OpenGLShader::LinkProgram() {
		m_GL.LinkProgram(m_RendererID);

		int success;
		m_GL.GetProgramiv(m_RendererID, GL_LINK_STATUS, &success);
		if (!success) {
			char infoLog[512];
			m_GL.GetProgramInfoLog(m_RendererID, 512, NULL, infoLog);
			s_Log.Error("Shader Linking Error:\n{}", infoLog);
			return false;
		}
		return true;
	}

	void OpenGLShader::SetMat4(const std::string& name, const float* data) {
		GLint location = m_GL.GetUniformLocation(m_RendererID, name.c_str());
		if (location != -1)
			m_GL.UniformMatrix4fv(location, 1, GL_FALSE, data);
	}

	void OpenGLShader::SetVec3(const std::string& name, const float* data) {
		GLint location = m_GL.GetUniformLocation(m_RendererID, name.c_str());
		if (location != -1)
			m_GL.Uniform3fv(location, 1, data);
	}

	void OpenGLShader::SetVec4(const std::string& name, const float* data) {
		GLint location = m_GL.GetUniformLocation(m_RendererID, name.c_str());
		if (location != -1)
			m_GL.Uniform4fv(location, 1, data);
	}

	void OpenGLShader::SetFloat(const std::string& name, float value) {
		GLint location = m_GL.GetUniformLocation(m_RendererID, name.c_str());
		if (location != -1)
			m_GL.Uniform1f(location, value);
	}

	void OpenGLShader::SetInt(const std::string& name, int value) {
		GLint location = m_GL.GetUniformLocation(m_RendererID, name.c_str());
		if (location != -1)
			m_GL.Uniform1i(location, value);
	}

	// ============================================================
	// 热加载：重新编译着色器
	// ============================================================

	bool OpenGLShader::Reload() {
		GLuint oldProgram = m_RendererID;
		m_RendererID = 0;

		std::string vSource = ReadFile(m_VertexPath);
		std::string fSource = ReadFile(m_FragmentPath);

		if (vSource.empty() || fSource.empty()) {
			s_Log.Error("[HotReload] Failed to read shader files");
			m_RendererID = oldProgram;  // 恢复旧程序
			return false;
		}

		uint32 vs = CompileShader(GL_VERTEX_SHADER, vSource);
		uint32 fs = CompileShader(GL_FRAGMENT_SHADER, fSource);

		if (!vs || !fs) {
			// 编译失败，恢复旧程序
			m_RendererID = oldProgram;
			if (vs) m_GL.DeleteShader(vs);
			if (fs) m_GL.DeleteShader(fs);
			return false;
		}

		GLuint newProgram = m_GL.CreateProgram();
		m_GL.AttachShader(newProgram, vs);
		m_GL.AttachShader(newProgram, fs);
		m_RendererID = newProgram;

		if (!LinkProgram()) {
			// 链接失败，恢复旧程序
			m_GL.DeleteProgram(newProgram);
			m_RendererID = oldProgram;
			m_GL.DeleteShader(vs);
			m_GL.DeleteShader(fs);
			return false;
		}

		m_GL.DeleteShader(vs);
		m_GL.DeleteShader(fs);

		// 释放旧程序
		if (oldProgram) {
			m_GL.DeleteProgram(oldProgram);
		}

		SetState(ResourceState::Loaded);
		s_Log.Info("[HotReload] Shader reloaded: {} + {}", m_VertexPath, m_FragmentPath);
		return true;
	}

	// ============================================================
	// 加载后初始化钩子：验证着色器已编译并链接
	// ============================================================

	bool OpenGLShader::PostLoad(IGraphicsFactory* factory) {
		(void)factory;
		// 构造函数已编译链接着色器，PostLoad 只需要验证状态
		return m_RendererID != 0;
	}

}
