#include "Engine/Rendering/DebugDraw/OpenGL/OpenGL46DebugDrawRenderer.h"

#include "Engine/Core/Log.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <cstddef>
#include <cstdio>

namespace Engine {
namespace Rendering {
namespace DebugDraw {

namespace {

const char* kVertexShaderSource = R"(
#version 460 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
uniform mat4 u_ViewProjection;
out vec4 vColor;
void main() {
    gl_Position = u_ViewProjection * vec4(aPosition, 1.0);
    vColor = aColor;
}
)";

const char* kFragmentShaderSource = R"(
#version 460 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";

void ClearGLErrors(GladGLContext& gl) {
    // 有界排空，避免 context lost 时无限循环。
    for (int i = 0; i < 16; ++i) {
        if (gl.GetError() == GL_NO_ERROR) return;
    }
}

struct GLStateSnapshot {
    GLboolean blendEnabled = GL_FALSE;
    GLboolean depthEnabled = GL_FALSE;
    GLboolean depthMask = GL_TRUE;
    GLint depthFunc = GL_LESS;
    GLint blendSrcRGB = GL_ONE;
    GLint blendDstRGB = GL_ZERO;
    GLint blendSrcAlpha = GL_ONE;
    GLint blendDstAlpha = GL_ZERO;
    GLint blendEqRGB = GL_FUNC_ADD;
    GLint blendEqAlpha = GL_FUNC_ADD;
    GLint program = 0;
    GLint vertexArray = 0;
    GLint arrayBuffer = 0;
    GLfloat lineWidth = 1.0f;

    explicit GLStateSnapshot(GladGLContext& gl) {
        gl.GetBooleanv(GL_BLEND, &blendEnabled);
        gl.GetBooleanv(GL_DEPTH_TEST, &depthEnabled);
        gl.GetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        gl.GetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        gl.GetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRGB);
        gl.GetIntegerv(GL_BLEND_DST_RGB, &blendDstRGB);
        gl.GetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
        gl.GetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
        gl.GetIntegerv(GL_BLEND_EQUATION_RGB, &blendEqRGB);
        gl.GetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEqAlpha);
        gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
        gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
        gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        gl.GetFloatv(GL_LINE_WIDTH, &lineWidth);
    }

    void Restore(GladGLContext& gl) const {
        gl.UseProgram(static_cast<GLuint>(program));
        gl.BindVertexArray(static_cast<GLuint>(vertexArray));
        gl.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer));

        if (blendEnabled) gl.Enable(GL_BLEND);
        else gl.Disable(GL_BLEND);
        gl.BlendFuncSeparate(static_cast<GLenum>(blendSrcRGB),
                             static_cast<GLenum>(blendDstRGB),
                             static_cast<GLenum>(blendSrcAlpha),
                             static_cast<GLenum>(blendDstAlpha));
        gl.BlendEquationSeparate(static_cast<GLenum>(blendEqRGB),
                                 static_cast<GLenum>(blendEqAlpha));

        if (depthEnabled) gl.Enable(GL_DEPTH_TEST);
        else gl.Disable(GL_DEPTH_TEST);
        gl.DepthFunc(static_cast<GLenum>(depthFunc));
        gl.DepthMask(depthMask);
        gl.LineWidth(lineWidth);
    }
};

struct StateRestoreGuard {
    GladGLContext& gl;
    const GLStateSnapshot& snapshot;
    ~StateRestoreGuard() { snapshot.Restore(gl); }
};

uint32 CompileShader(GladGLContext& gl, GLenum type, const char* source) {
    const GLuint shader = gl.CreateShader(type);
    if (shader == 0) return 0;

    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);

    GLint success = GL_FALSE;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE) {
        GLchar infoLog[1024] = {};
        gl.GetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        ENGINE_LOG_ERROR("DebugDraw", "shader compile failed: {}", infoLog);
        gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

OpenGL46DebugDrawRenderer::OpenGL46DebugDrawRenderer(GladGLContext& gl)
    : m_GL(gl) {
}

OpenGL46DebugDrawRenderer::~OpenGL46DebugDrawRenderer() {
    (void)Shutdown();
}

Status OpenGL46DebugDrawRenderer::CheckContextSupport() const {
    if (!glfwGetCurrentContext()) {
        return Status::BackendFailure;
    }

    const char* versionString =
        reinterpret_cast<const char*>(m_GL.GetString(GL_VERSION));
    if (!versionString) {
        return Status::BackendFailure;
    }

    int major = 0;
    int minor = 0;
    if (std::sscanf(versionString, "%d.%d", &major, &minor) != 2) {
        return Status::BackendFailure;
    }
    if (major < 4 || (major == 4 && minor < 6)) {
        return Status::Unsupported;
    }

    GLint profileMask = 0;
    m_GL.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
    if ((profileMask & GL_CONTEXT_CORE_PROFILE_BIT) == 0) {
        return Status::Unsupported;
    }

    return Status::Ok;
}

bool OpenGL46DebugDrawRenderer::IsSupported() const {
    return CheckContextSupport() == Status::Ok;
}

Status OpenGL46DebugDrawRenderer::CompileProgram() {
    const uint32 vertexShader =
        CompileShader(m_GL, GL_VERTEX_SHADER, kVertexShaderSource);
    if (vertexShader == 0) return Status::BackendFailure;

    const uint32 fragmentShader =
        CompileShader(m_GL, GL_FRAGMENT_SHADER, kFragmentShaderSource);
    if (fragmentShader == 0) {
        m_GL.DeleteShader(vertexShader);
        return Status::BackendFailure;
    }

    m_Program = m_GL.CreateProgram();
    if (m_Program == 0) {
        m_GL.DeleteShader(vertexShader);
        m_GL.DeleteShader(fragmentShader);
        return Status::BackendFailure;
    }

    m_GL.AttachShader(m_Program, vertexShader);
    m_GL.AttachShader(m_Program, fragmentShader);
    m_GL.LinkProgram(m_Program);
    m_GL.DeleteShader(vertexShader);
    m_GL.DeleteShader(fragmentShader);

    GLint success = GL_FALSE;
    m_GL.GetProgramiv(m_Program, GL_LINK_STATUS, &success);
    if (success != GL_TRUE) {
        GLchar infoLog[1024] = {};
        m_GL.GetProgramInfoLog(m_Program, sizeof(infoLog), nullptr, infoLog);
        ENGINE_LOG_ERROR("DebugDraw", "program link failed: {}", infoLog);
        m_GL.DeleteProgram(m_Program);
        m_Program = 0;
        return Status::BackendFailure;
    }

    m_ViewProjectionLocation =
        m_GL.GetUniformLocation(m_Program, "u_ViewProjection");
    if (m_ViewProjectionLocation < 0) {
        m_GL.DeleteProgram(m_Program);
        m_Program = 0;
        return Status::BackendFailure;
    }
    return Status::Ok;
}

Status OpenGL46DebugDrawRenderer::CreateBuffers() {
    m_GL.GenVertexArrays(1, &m_VAO);
    m_GL.GenBuffers(1, &m_VBO);
    if (m_VAO == 0 || m_VBO == 0) {
        DestroyResources();
        return Status::BackendFailure;
    }

    m_GL.BindVertexArray(m_VAO);
    m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);
    m_GL.BufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);

    m_GL.EnableVertexAttribArray(0);
    m_GL.VertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<const void*>(offsetof(Vertex, position)));
    m_GL.EnableVertexAttribArray(1);
    m_GL.VertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<const void*>(offsetof(Vertex, color)));

    m_GL.BindBuffer(GL_ARRAY_BUFFER, 0);
    m_GL.BindVertexArray(0);

    if (m_GL.GetError() != GL_NO_ERROR) {
        DestroyResources();
        return Status::BackendFailure;
    }
    return Status::Ok;
}

Status OpenGL46DebugDrawRenderer::Initialize() {
    if (m_Initialized) return Status::AlreadyInitialized;

    m_OwnerThread = std::this_thread::get_id();
    const Status support = CheckContextSupport();
    if (support != Status::Ok) {
        ENGINE_LOG_ERROR("DebugDraw", "OpenGL context unsupported: {}",
                         StatusToString(support));
        return support;
    }

    const Status programStatus = CompileProgram();
    if (programStatus != Status::Ok) {
        DestroyResources();
        return programStatus;
    }

    const Status bufferStatus = CreateBuffers();
    if (bufferStatus != Status::Ok) {
        DestroyResources();
        return bufferStatus;
    }

    m_Initialized = true;
    return Status::Ok;
}

Status OpenGL46DebugDrawRenderer::Shutdown() {
    if (!m_Initialized) return Status::Ok;
    if (std::this_thread::get_id() != m_OwnerThread) {
        return Status::WrongThread;
    }
    if (!glfwGetCurrentContext()) {
        // 上下文缺失时不执行任何删除调用；保留资源以便上下文恢复后重试。
        return Status::BackendFailure;
    }

    DestroyResources();
    const GLenum error = m_GL.GetError();
    m_Initialized = false;
    return error == GL_NO_ERROR ? Status::Ok : Status::BackendFailure;
}

void OpenGL46DebugDrawRenderer::DestroyResources() {
    if (m_Program != 0) {
        m_GL.DeleteProgram(m_Program);
        m_Program = 0;
    }
    if (m_VAO != 0) {
        m_GL.DeleteVertexArrays(1, &m_VAO);
        m_VAO = 0;
    }
    if (m_VBO != 0) {
        m_GL.DeleteBuffers(1, &m_VBO);
        m_VBO = 0;
    }
    m_ViewProjectionLocation = -1;
}

Status OpenGL46DebugDrawRenderer::Render(
    const View& view, const FrameGeometry& geometry) {
    if (!m_Initialized) return Status::NotInitialized;
    if (std::this_thread::get_id() != m_OwnerThread) {
        return Status::WrongThread;
    }
    if (!glfwGetCurrentContext()) return Status::BackendFailure;

    if ((geometry.depthVertexCount % 2) != 0 ||
        (geometry.xrayVertexCount % 2) != 0) {
        return Status::InvalidArgument;
    }
    if (geometry.depthVertexCount > 0 && !geometry.depthVertices) {
        return Status::InvalidArgument;
    }
    if (geometry.xrayVertexCount > 0 && !geometry.xrayVertices) {
        return Status::InvalidArgument;
    }
    const uint64 totalVertices =
        static_cast<uint64>(geometry.depthVertexCount) +
        static_cast<uint64>(geometry.xrayVertexCount);
    if (totalVertices > static_cast<uint64>(kMaxVerticesPerFrame)) {
        return Status::InvalidArgument;
    }
    if (totalVertices == 0) return Status::Ok;

    ClearGLErrors(m_GL);
    const GLStateSnapshot snapshot(m_GL);
    StateRestoreGuard restoreGuard{m_GL, snapshot};

    m_GL.UseProgram(m_Program);
    m_GL.BindVertexArray(m_VAO);
    m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);
    m_GL.UniformMatrix4fv(m_ViewProjectionLocation, 1, GL_FALSE,
                          view.viewProjection.data);

    m_GL.Enable(GL_BLEND);
    m_GL.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                           GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_GL.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    m_GL.LineWidth(1.0f);

    auto drawBatch = [&](const Vertex* vertices, uint32 vertexCount) {
        if (vertexCount == 0) return;
        m_GL.BufferData(GL_ARRAY_BUFFER,
                        static_cast<GLsizeiptr>(vertexCount * sizeof(Vertex)),
                        vertices, GL_STREAM_DRAW);
        m_GL.DrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertexCount));
    };

    if (geometry.depthVertexCount > 0) {
        m_GL.Enable(GL_DEPTH_TEST);
        m_GL.DepthMask(GL_FALSE);
        m_GL.DepthFunc(GL_LEQUAL);
        drawBatch(geometry.depthVertices, geometry.depthVertexCount);
    }

    if (geometry.xrayVertexCount > 0) {
        m_GL.Disable(GL_DEPTH_TEST);
        m_GL.DepthMask(GL_FALSE);
        drawBatch(geometry.xrayVertices, geometry.xrayVertexCount);
    }

    const GLenum error = m_GL.GetError();
    return error == GL_NO_ERROR ? Status::Ok : Status::BackendFailure;
}

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
