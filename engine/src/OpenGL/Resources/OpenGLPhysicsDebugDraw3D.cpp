#include "OpenGLPhysicsDebugDraw3D.h"
#include <cstring>
#include <cmath>

namespace Engine {

OpenGLPhysicsDebugDraw3D::OpenGLPhysicsDebugDraw3D(GladGLContext& gl)
    : m_GL(gl)
{
    std::memset(m_ViewProj, 0, sizeof(m_ViewProj));
    m_ViewProj[0] = m_ViewProj[5] = m_ViewProj[10] = m_ViewProj[15] = 1.0f;

    // 编译简单着色器
    const char* vertSrc = R"(
        #version 460 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec4 aColor;
        uniform mat4 u_VP;
        out vec4 vColor;
        void main() {
            gl_Position = u_VP * vec4(aPos, 1.0);
            vColor = aColor;
        }
    )";
    const char* fragSrc = R"(
        #version 460 core
        in vec4 vColor;
        out vec4 FragColor;
        void main() { FragColor = vColor; }
    )";

    auto compile = [&](GLenum type, const char* src) -> uint32 {
        uint32 id = m_GL.CreateShader(type);
        m_GL.ShaderSource(id, 1, &src, nullptr);
        m_GL.CompileShader(id);
        return id;
    };
    auto link = [&](uint32 vs, uint32 fs) -> uint32 {
        uint32 prog = m_GL.CreateProgram();
        m_GL.AttachShader(prog, vs);
        m_GL.AttachShader(prog, fs);
        m_GL.LinkProgram(prog);
        return prog;
    };

    uint32 vs = compile(GL_VERTEX_SHADER, vertSrc);
    uint32 fs = compile(GL_FRAGMENT_SHADER, fragSrc);
    m_ShaderID = link(vs, fs);
    m_GL.DeleteShader(vs);
    m_GL.DeleteShader(fs);
    m_UniformVP = m_GL.GetUniformLocation(m_ShaderID, "u_VP");

    m_GL.GenVertexArrays(1, &m_VAO);
    m_GL.GenBuffers(1, &m_VBO);
    m_Lines.reserve(4096);
}

OpenGLPhysicsDebugDraw3D::~OpenGLPhysicsDebugDraw3D() {
    if (m_ShaderID) m_GL.DeleteProgram(m_ShaderID);
    if (m_VAO) m_GL.DeleteVertexArrays(1, &m_VAO);
    if (m_VBO) m_GL.DeleteBuffers(1, &m_VBO);
}

void OpenGLPhysicsDebugDraw3D::Clear() {
    m_Lines.clear();
}

void OpenGLPhysicsDebugDraw3D::SubmitLines() {
    if (m_Lines.empty()) return;
    m_GL.UseProgram(m_ShaderID);
    m_GL.UniformMatrix4fv(m_UniformVP, 1, GL_FALSE, m_ViewProj);

    m_GL.BindVertexArray(m_VAO);
    m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);
    m_GL.BufferData(GL_ARRAY_BUFFER, m_Lines.size() * sizeof(LineVertex), m_Lines.data(), GL_DYNAMIC_DRAW);

    m_GL.EnableVertexAttribArray(0);
    m_GL.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)offsetof(LineVertex, pos));
    m_GL.EnableVertexAttribArray(1);
    m_GL.VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)offsetof(LineVertex, color));

    m_GL.Enable(GL_BLEND);
    m_GL.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_GL.DrawArrays(GL_LINES, 0, (GLsizei)(m_Lines.size() / 2 * 2));
    m_GL.BindVertexArray(0);
    m_GL.UseProgram(0);
}

void OpenGLPhysicsDebugDraw3D::DrawLine(const Vec3& from, const Vec3& to, const Vec4& color) {
    m_Lines.push_back({from, color});
    m_Lines.push_back({to, color});
}

void OpenGLPhysicsDebugDraw3D::DrawTriangle(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec4& color, bool wireframe) {
    if (wireframe) {
        DrawLine(v0, v1, color);
        DrawLine(v1, v2, color);
        DrawLine(v2, v0, color);
    }
}

void OpenGLPhysicsDebugDraw3D::DrawSphere(const Vec3& center, float radius, const Vec4& color, int32 segments) {
    // 绘制 3 个正交方向上的圆环
    for (int axis = 0; axis < 3; ++axis) {
        Vec3 prev;
        for (int i = 0; i <= segments; ++i) {
            float theta = (float)i / segments * 6.2832f;
            float c = std::cos(theta) * radius;
            float s = std::sin(theta) * radius;
            Vec3 p = center;
            if (axis == 0) { p.y += c; p.z += s; }
            else if (axis == 1) { p.x += c; p.z += s; }
            else { p.x += c; p.y += s; }
            if (i > 0) DrawLine(prev, p, color);
            prev = p;
        }
    }
}

void OpenGLPhysicsDebugDraw3D::DrawBox(const Vec3& center, const Vec3& half, const Vec4& color, bool wireframe) {
    if (!wireframe) return;
    Vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        corners[i] = Vec3(
            center.x + ((i & 1) ? half.x : -half.x),
            center.y + ((i & 2) ? half.y : -half.y),
            center.z + ((i & 4) ? half.z : -half.z));
    }
    int edges[12][2] = {
        {0,1},{1,3},{3,2},{2,0},
        {4,5},{5,7},{7,6},{6,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    for (auto& e : edges) DrawLine(corners[e[0]], corners[e[1]], color);
}

void OpenGLPhysicsDebugDraw3D::DrawCapsule(const Vec3& center, float radius, float height, const Vec4& color, bool wireframe) {
    if (!wireframe) return;
    float halfH = height * 0.5f;
    // 上下半球 + 圆柱体边线
    for (int side = -1; side <= 1; side += 2) {
        Vec3 capCenter = center + Vec3(0, halfH * side, 0);
        DrawSphere(capCenter, radius, color, 12);
    }
    // 圆柱体 4 条竖直线
    float r = radius;
    for (int i = 0; i < 4; ++i) {
        float a = (float)i / 4 * 6.2832f;
        float dx = std::cos(a) * r;
        float dz = std::sin(a) * r;
        DrawLine(Vec3(center.x + dx, center.y - halfH, center.z + dz),
                 Vec3(center.x + dx, center.y + halfH, center.z + dz), color);
    }
}

void OpenGLPhysicsDebugDraw3D::DrawCylinder(const Vec3& center, float radius, float height, const Vec4& color, bool wireframe) {
    if (!wireframe) return;
    float halfH = height * 0.5f;
    for (int ring = -1; ring <= 1; ring += 2) {
        float y = center.y + halfH * ring;
        Vec3 prev;
        for (int i = 0; i <= 16; ++i) {
            float a = (float)i / 16 * 6.2832f;
            Vec3 p(center.x + std::cos(a) * radius, y, center.z + std::sin(a) * radius);
            if (i > 0) DrawLine(prev, p, color);
            prev = p;
        }
    }
    for (int i = 0; i < 8; ++i) {
        float a = (float)i / 8 * 6.2832f;
        float dx = std::cos(a) * radius;
        float dz = std::sin(a) * radius;
        DrawLine(Vec3(center.x + dx, center.y - halfH, center.z + dz),
                 Vec3(center.x + dx, center.y + halfH, center.z + dz), color);
    }
}

void OpenGLPhysicsDebugDraw3D::DrawText3D(const Vec3&, const char*, const Vec4&) {
    // 暂不实现 3D 文本
}

void OpenGLPhysicsDebugDraw3D::DrawAxes(const Mat4& transform, float length) {
    Vec3 origin(transform.data[12], transform.data[13], transform.data[14]);
    Vec3 right(transform.data[0], transform.data[1], transform.data[2]);
    Vec3 up(transform.data[4], transform.data[5], transform.data[6]);
    Vec3 fwd(transform.data[8], transform.data[9], transform.data[10]);
    float l = length;
    auto norm = [l](Vec3 v) {
        float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
        if (len > 0) { v.x /= len; v.y /= len; v.z /= len; }
        return Vec3(v.x*l, v.y*l, v.z*l);
    };
    DrawLine(origin, origin + norm(right), Vec4(1,0,0,1));
    DrawLine(origin, origin + norm(up), Vec4(0,1,0,1));
    DrawLine(origin, origin + norm(fwd), Vec4(0,0,1,1));
}

void OpenGLPhysicsDebugDraw3D::Flush() {
    SubmitLines();
}

} // namespace Engine