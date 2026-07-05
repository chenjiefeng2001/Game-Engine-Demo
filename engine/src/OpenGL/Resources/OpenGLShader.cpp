#include "OpenGLShader.h"

#include "Engine/Core/Log.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <glad/gl.h>

namespace {
    Engine::Logger s_Log("OpenGLShader");
}

namespace Engine {

    // ════════════════════════════════════════════════
    // 映射表: ShaderStageType ↔ GLenum
    // ════════════════════════════════════════════════

    unsigned int OpenGLShader::StageTypeToGL(ShaderStageType type) {
        switch (type) {
            case ShaderStageType::Vertex:         return GL_VERTEX_SHADER;
            case ShaderStageType::Fragment:       return GL_FRAGMENT_SHADER;
            case ShaderStageType::Geometry:       return GL_GEOMETRY_SHADER;
            case ShaderStageType::TessControl:    return GL_TESS_CONTROL_SHADER;
            case ShaderStageType::TessEvaluation: return GL_TESS_EVALUATION_SHADER;
            case ShaderStageType::Compute:        return GL_COMPUTE_SHADER;
        }
        return GL_VERTEX_SHADER;
    }

    ShaderStageType OpenGLShader::GLToStageType(unsigned int glType) {
        switch (glType) {
            case GL_VERTEX_SHADER:          return ShaderStageType::Vertex;
            case GL_FRAGMENT_SHADER:        return ShaderStageType::Fragment;
            case GL_GEOMETRY_SHADER:        return ShaderStageType::Geometry;
            case GL_TESS_CONTROL_SHADER:    return ShaderStageType::TessControl;
            case GL_TESS_EVALUATION_SHADER: return ShaderStageType::TessEvaluation;
            case GL_COMPUTE_SHADER:         return ShaderStageType::Compute;
        }
        return ShaderStageType::Vertex;
    }

    // ════════════════════════════════════════════════
    // 构造 / 析构
    // ════════════════════════════════════════════════

    OpenGLShader::OpenGLShader(const std::string& vertexPath,
                                const std::string& fragmentPath,
                                GladGLContext& gl)
        : Shader(vertexPath + "|" + fragmentPath)
        , m_GL(gl)
        , m_VertexPath(vertexPath)
        , m_FragmentPath(fragmentPath)
    {
        // 构造时自动从文件加载两阶段版本（向后兼容）
        std::string vSource = ReadFile(vertexPath);
        std::string fSource = ReadFile(fragmentPath);

        if (vSource.empty() || fSource.empty()) {
            SetState(ResourceState::Failed);
            return;
        }

        m_Stages.push_back(ShaderStage::FromGLSL(ShaderStageType::Vertex, vSource));
        m_Stages.push_back(ShaderStage::FromGLSL(ShaderStageType::Fragment, fSource));

        uint32 vs = CompileShader(GL_VERTEX_SHADER, vSource);
        uint32 fs = CompileShader(GL_FRAGMENT_SHADER, fSource);

        if (!vs || !fs) {
            SetState(ResourceState::Failed);
            return;
        }

        m_RendererID = m_GL.CreateProgram();
        m_GL.AttachShader(m_RendererID, vs);
        m_GL.AttachShader(m_RendererID, fs);

        if (LinkProgram()) {
            SetState(ResourceState::Loaded);
            ReflectUniforms();
        } else {
            SetState(ResourceState::Failed);
        }

        m_GL.DeleteShader(vs);
        m_GL.DeleteShader(fs);
    }

    OpenGLShader::~OpenGLShader() {
        if (m_RendererID) {
            m_GL.DeleteProgram(m_RendererID);
        }
    }

    // ════════════════════════════════════════════════
    // 多阶段加载（新接口）
    // ════════════════════════════════════════════════

    bool OpenGLShader::LoadStages(const std::vector<ShaderStage>& stages) {
        if (stages.empty()) {
            s_Log.Error("LoadStages: empty stage list");
            return false;
        }

        // 收集要编译的阶段
        std::vector<uint32> shaderIDs;
        shaderIDs.reserve(stages.size());

        for (const auto& stage : stages) {
            uint32 id = CompileShaderFromStage(stage);
            if (id == 0) {
                // 编译失败，清理已创建的
                for (uint32 sid : shaderIDs)
                    m_GL.DeleteShader(sid);
                return false;
            }
            shaderIDs.push_back(id);
        }

        // 创建新程序
        GLuint newProgram = m_GL.CreateProgram();
        for (uint32 id : shaderIDs) {
            m_GL.AttachShader(newProgram, id);
        }

        // 保存旧程序（用于热加载失败回滚）
        GLuint oldProgram = m_RendererID;

        m_RendererID = newProgram;

        if (!LinkProgram()) {
            // 链接失败，回滚
            m_RendererID = oldProgram;
            m_GL.DeleteProgram(newProgram);
            for (uint32 id : shaderIDs)
                m_GL.DeleteShader(id);
            return false;
        }

        // 清理旧程序和旧的 shader 对象
        if (oldProgram) {
            m_GL.DeleteProgram(oldProgram);
        }
        for (uint32 id : shaderIDs) {
            m_GL.DeleteShader(id);
        }

        // 保存阶段描述（用于热加载和查询）
        m_Stages = stages;

        // 更新 vertex/fragment 路径（向后兼容）
        for (const auto& stage : stages) {
            if (stage.type == ShaderStageType::Vertex)
                m_VertexPath = stage.source;
            else if (stage.type == ShaderStageType::Fragment)
                m_FragmentPath = stage.source;
        }

        // 反射
        ReflectUniforms();
        m_ReflectionDirty = false;

        SetState(ResourceState::Loaded);
        return true;
    }

    // ════════════════════════════════════════════════
    // Uniform Block / Storage Block
    // ════════════════════════════════════════════════

    void OpenGLShader::BindUniformBlock(const std::string& name,
                                         uint64 bufferID,
                                         uint32 binding) {
        GLuint index = m_GL.GetUniformBlockIndex(m_RendererID, name.c_str());
        if (index != GL_INVALID_INDEX) {
            m_GL.UniformBlockBinding(m_RendererID, index, binding);
            m_GL.BindBufferBase(GL_UNIFORM_BUFFER, binding, (GLuint)bufferID);
        }
    }

    void OpenGLShader::BindStorageBlock(const std::string& name,
                                         uint64 bufferID,
                                         uint32 binding) {
        GLuint index = m_GL.GetProgramResourceIndex(
            m_RendererID, GL_SHADER_STORAGE_BLOCK, name.c_str());
        if (index != GL_INVALID_INDEX) {
            m_GL.ShaderStorageBlockBinding(m_RendererID, index, binding);
            m_GL.BindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, (GLuint)bufferID);
        }
    }

    // ════════════════════════════════════════════════
    // 反射
    // ════════════════════════════════════════════════

    void OpenGLShader::ReflectUniforms() {
        m_Reflection = ShaderReflection{};
        if (!m_RendererID) return;

        GLint uniformCount = 0;
        m_GL.GetProgramiv(m_RendererID, GL_ACTIVE_UNIFORMS, &uniformCount);

        GLint maxNameLen = 0;
        m_GL.GetProgramiv(m_RendererID, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxNameLen);

        std::vector<char> nameBuf(std::max(256, maxNameLen));

        for (GLint i = 0; i < uniformCount; ++i) {
            GLsizei length = 0;
            GLint   size  = 0;
            GLenum  type  = GL_NONE;
            m_GL.GetActiveUniform(m_RendererID, (GLuint)i, (GLsizei)nameBuf.size(),
                                   &length, &size, &type, nameBuf.data());
            if (length > 0) {
                std::string name(nameBuf.data(), length);
                GLint location = m_GL.GetUniformLocation(m_RendererID, name.c_str());
                m_Reflection.uniforms.push_back({name, location, size, (uint32)type, -1});
            }
        }
    }

    const ShaderReflection& OpenGLShader::GetReflection() const {
        if (m_ReflectionDirty && m_RendererID) {
            const_cast<OpenGLShader*>(this)->ReflectUniforms();
            m_ReflectionDirty = false;
        }
        return m_Reflection;
    }

    ShaderStageType OpenGLShader::GetStageType(uint32 index) const {
        if (index < m_Stages.size())
            return m_Stages[index].type;
        return ShaderStageType::Vertex;
    }

    bool OpenGLShader::IsCompute() const {
        for (const auto& stage : m_Stages) {
            if (stage.type == ShaderStageType::Compute)
                return true;
        }
        return false;
    }

    // ════════════════════════════════════════════════
    // 旧接口 uniform 设置
    // ════════════════════════════════════════════════

    void OpenGLShader::Bind() const { m_GL.UseProgram(m_RendererID); }
    void OpenGLShader::Unbind() const { m_GL.UseProgram(0); }

    void OpenGLShader::SetMat4(const std::string& name, const float* data) {
        GLint location = m_GL.GetUniformLocation(m_RendererID, name.c_str());
        if (location != -1)
            m_GL.UniformMatrix4fv(location, 1, GL_FALSE, data);
    }

    void OpenGLShader::SetVec2(const std::string& name, const float* data) {
        GLint location = m_GL.GetUniformLocation(m_RendererID, name.c_str());
        if (location != -1)
            m_GL.Uniform2fv(location, 1, data);
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

    void OpenGLShader::SetMat3(const std::string& name, const float* data) {
        GLint location = m_GL.GetUniformLocation(m_RendererID, name.c_str());
        if (location != -1)
            m_GL.UniformMatrix3fv(location, 1, GL_FALSE, data);
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

    // ════════════════════════════════════════════════
    // 内部工具
    // ════════════════════════════════════════════════

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
        if (source.empty()) return 0;

        uint32 id = m_GL.CreateShader(type);
        const char* src = source.c_str();
        m_GL.ShaderSource(id, 1, &src, nullptr);
        m_GL.CompileShader(id);

        int result;
        m_GL.GetShaderiv(id, GL_COMPILE_STATUS, &result);
        if (!result) {
            char infoLog[512];
            m_GL.GetShaderInfoLog(id, 512, NULL, infoLog);
            const char* typeName = "unknown";
            switch (type) {
                case GL_VERTEX_SHADER:          typeName = "vertex";    break;
                case GL_FRAGMENT_SHADER:        typeName = "fragment";  break;
                case GL_GEOMETRY_SHADER:        typeName = "geometry";  break;
                case GL_TESS_CONTROL_SHADER:    typeName = "tess_control"; break;
                case GL_TESS_EVALUATION_SHADER: typeName = "tess_eval"; break;
                case GL_COMPUTE_SHADER:         typeName = "compute";   break;
            }
            s_Log.Error("Shader Compilation Error ({}):\n{}", typeName, infoLog);
            m_GL.DeleteShader(id);
            return 0;
        }
        return id;
    }

    uint32 OpenGLShader::CompileShaderFromStage(const ShaderStage& stage) {
        unsigned int glType = StageTypeToGL(stage.type);

        switch (stage.sourceType) {
            case ShaderSourceType::GLSL: {
                // 如果是文件路径而非源码，读取文件
                std::string source = stage.source;
                // 简单检测：如果包含换行符则视为源码，否则视为文件路径
                if (source.find('\n') == std::string::npos) {
                    source = ReadFile(stage.source);
                }
                return CompileShader(glType, source);
            }
            case ShaderSourceType::SPIRV: {
                // SPIR-V 支持（需要 GL 4.6 + ARB_gl_spirv）
                // 目前跳过，待集成 shaderc 后实现
                s_Log.Warn("SPIR-V shader loading not yet implemented, falling back");
                return 0;
            }
            case ShaderSourceType::HLSL:
            case ShaderSourceType::MSL:
                s_Log.Error("HLSL/MSL shaders require SPIR-V cross-compilation (shaderc)");
                return 0;
        }
        return 0;
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

    // ════════════════════════════════════════════════
    // 热加载
    // ════════════════════════════════════════════════

    bool OpenGLShader::Reload() {
        // 如果有保存的阶段描述，使用多阶段重载
        if (!m_Stages.empty()) {
            // 重新读取所有阶段的源码
            std::vector<ShaderStage> reloadedStages = m_Stages;
            for (auto& stage : reloadedStages) {
                if (stage.sourceType == ShaderSourceType::GLSL &&
                    stage.source.find('\n') == std::string::npos) {
                    stage.source = ReadFile(stage.source);
                }
            }
            return LoadStages(reloadedStages);
        }

        // 否则使用旧的两阶段路径重载
        GLuint oldProgram = m_RendererID;
        m_RendererID = 0;

        std::string vSource = ReadFile(m_VertexPath);
        std::string fSource = ReadFile(m_FragmentPath);

        if (vSource.empty() || fSource.empty()) {
            m_RendererID = oldProgram;
            return false;
        }

        uint32 vs = CompileShader(GL_VERTEX_SHADER, vSource);
        uint32 fs = CompileShader(GL_FRAGMENT_SHADER, fSource);

        if (!vs || !fs) {
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
            m_GL.DeleteProgram(newProgram);
            m_RendererID = oldProgram;
            m_GL.DeleteShader(vs);
            m_GL.DeleteShader(fs);
            return false;
        }

        m_GL.DeleteShader(vs);
        m_GL.DeleteShader(fs);
        if (oldProgram) m_GL.DeleteProgram(oldProgram);

        ReflectUniforms();
        SetState(ResourceState::Loaded);
        s_Log.Info("[HotReload] Shader reloaded: {} + {}", m_VertexPath, m_FragmentPath);
        return true;
    }

    bool OpenGLShader::PostLoad(IGraphicsFactory* factory) {
        (void)factory;
        return m_RendererID != 0;
    }

} // namespace Engine
