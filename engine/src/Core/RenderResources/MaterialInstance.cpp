#include "Engine/Core/RenderResources/MaterialInstance.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/gl.h>
#include <sstream>

namespace Engine {

    // ══════════════════════════════════════════════════════════════
    // MaterialInstance 实现
    // ══════════════════════════════════════════════════════════════

    std::shared_ptr<MaterialInstance> MaterialInstance::Create(IGraphicsFactory& factory) {
        return std::shared_ptr<MaterialInstance>(new MaterialInstance(factory));
    }

    bool MaterialInstance::CompileFromGLSL(const std::string& vertexSource,
                                           const std::string& fragmentSource) {
        m_VertexSrc = vertexSource;
        m_FragmentSrc = fragmentSource;

        // 通过 RHI 工厂创建 Shader（遵循抽象工厂原则）
        // 使用 CreateShaderFromStages 因为 CreateShader 只接受文件路径
        std::vector<ShaderStage> stages;
        stages.push_back(ShaderStage::FromGLSL(ShaderStageType::Vertex, vertexSource));
        stages.push_back(ShaderStage::FromGLSL(ShaderStageType::Fragment, fragmentSource));
        m_Shader = m_Factory.CreateShaderFromStages(stages);

        if (!m_Shader) {
            m_CompileLog = "Failed to create shader through factory";
            return false;
        }

        m_IsDirty = false;
        return true;
    }

    bool MaterialInstance::CompileFromShaderGraph(const std::string& fragmentSource) {
        // 内置默认 vertex shader（标准 3D 变换）
        const std::string vertexSrc = R"(
#version 460 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Projection;
uniform mat3 u_NormalMatrix;

out vec3 v_PositionWS;
out vec3 v_NormalWS;
out vec2 v_Uv;
out vec3 v_ViewDir;
out vec3 v_TangentWS;
out float v_Time;

uniform float u_Time;

void main() {
    vec4 worldPos = u_Model * vec4(a_Position, 1.0);
    v_PositionWS = worldPos.xyz;
    v_NormalWS = normalize(u_NormalMatrix * a_Normal);
    v_Uv = a_TexCoord;
    v_ViewDir = normalize((inverse(u_View) * vec4(0,0,0,1)).xyz - worldPos.xyz);
    v_TangentWS = normalize(u_NormalMatrix * a_Tangent);
    v_Time = u_Time;
    gl_Position = u_Projection * u_View * worldPos;
}
)";

        std::string wrappedFS = R"(
#version 460 core
#define PI 3.1415926535

in vec3 v_PositionWS;
in vec3 v_NormalWS;
in vec2 v_Uv;
in vec3 v_ViewDir;
in vec3 v_TangentWS;
in float v_Time;

out vec4 FragColor;
)";
        wrappedFS += fragmentSource;

        // Replace HLSL → GLSL
        auto replace = [](std::string& str, const std::string& from, const std::string& to) {
            size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::string::npos) {
                str.replace(pos, from.length(), to);
                pos += to.length();
            }
        };
        replace(wrappedFS, "float4(", "vec4(");
        replace(wrappedFS, "float3(", "vec3(");
        replace(wrappedFS, "float2(", "vec2(");
        replace(wrappedFS, " : SV_Target", "");
        replace(wrappedFS, "return float4(", "FragColor = vec4(");
        replace(wrappedFS, "};", "}");

        // Ensure main function exists
        if (wrappedFS.find("void main()") == std::string::npos &&
            wrappedFS.find("void main(") == std::string::npos) {
            wrappedFS += "\nvoid main() { FragColor = vec4(1.0); }\n";
        }

        return CompileFromGLSL(vertexSrc, wrappedFS);
    }

    void MaterialInstance::Bind(IRenderContext& ctx) {
        if (!m_Shader) return;
        m_Shader->Bind();

        // 应用所有参数
        for (auto& [name, param] : m_Parameters) {
            std::visit([this, &name](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, float>) {
                    m_Shader->SetFloat("u_" + name, arg);
                } else if constexpr (std::is_same_v<T, glm::vec2>) {
                    m_Shader->SetVec2("u_" + name, glm::value_ptr(arg));
                } else if constexpr (std::is_same_v<T, glm::vec3>) {
                    m_Shader->SetVec3("u_" + name, glm::value_ptr(arg));
                } else if constexpr (std::is_same_v<T, glm::vec4>) {
                    m_Shader->SetVec4("u_" + name, glm::value_ptr(arg));
                } else if constexpr (std::is_same_v<T, int>) {
                    m_Shader->SetInt("u_" + name, arg);
                } else if constexpr (std::is_same_v<T, bool>) {
                    m_Shader->SetInt("u_" + name, arg ? 1 : 0);
                }
            }, param.value);
        }
    }

    void MaterialInstance::Unbind() {
        if (m_Shader) m_Shader->Unbind();
    }

    // ══════════════════════════════════════════════════════════════
    // MaterialPreview
    // ══════════════════════════════════════════════════════════════

    MaterialPreview::MaterialPreview() = default;

    void MaterialPreview::Init(IGraphicsFactory& factory) {
        // 创建球体 VAO（经纬球）
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
        const int sectors = 32, stacks = 24;
        const float radius = 1.0f;

        for (int i = 0; i <= stacks; ++i) {
            float phi = 3.14159265f * i / stacks;
            for (int j = 0; j <= sectors; ++j) {
                float theta = 2.0f * 3.14159265f * j / sectors;
                vertices.push_back(radius * sin(phi) * cos(theta));
                vertices.push_back(radius * cos(phi));
                vertices.push_back(radius * sin(phi) * sin(theta));
                vertices.push_back(sin(phi) * cos(theta));
                vertices.push_back(cos(phi));
                vertices.push_back(sin(phi) * sin(theta));
                vertices.push_back((float)j / sectors);
                vertices.push_back((float)i / stacks);
            }
        }
        for (int i = 0; i < stacks; ++i) {
            for (int j = 0; j < sectors; ++j) {
                int first = i * (sectors + 1) + j;
                int second = first + sectors + 1;
                indices.push_back(first);
                indices.push_back(second);
                indices.push_back(first + 1);
                indices.push_back(second);
                indices.push_back(second + 1);
                indices.push_back(first + 1);
            }
        }

        auto vb = factory.CreateVertexBuffer(vertices.data(), (uint32)(vertices.size() * sizeof(float)));
        auto ib = factory.CreateIndexBuffer(indices.data(), (uint32)(indices.size() * sizeof(uint32_t)));
        m_SphereVAO = factory.CreateVertexArray();

        // Vertex layout: pos(3) + normal(3) + texcoord(2) = 8 floats
        VertexAttribute attrs[3] = {
            {0, 3, 8 * sizeof(float), 0},                              // a_Position
            {1, 3, 8 * sizeof(float), 3 * sizeof(float)},               // a_Normal
            {2, 2, 8 * sizeof(float), 6 * sizeof(float)}                // a_TexCoord
        };
        m_SphereVAO->AddVertexBuffer(vb, attrs, 3);
        m_SphereVAO->SetIndexBuffer(ib);

        // 创建预览 shader（通过文件路径）
        // 如果文件不存在则创建一个简单的 unlit shader
        m_LightShader = factory.CreateShader("assets/shaders/3d_lit.vert", "assets/shaders/3d_lit.frag");

        m_Initialized = true;
    }

    void MaterialPreview::Render(IRenderContext& ctx) {
        if (!m_Initialized || !m_Material || !m_Material->IsReady()) return;
        // 材质预览 — 当前为架构占位
        // 实际预览流程：
        // 1. 绑定材质 shader: m_Material->Bind(ctx)
        // 2. 设置 MVP 矩阵
        // 3. 绘制球体 mesh
        // 4. 解绑
        // 完整的预览渲染将通过 ViewportPanel 中的 FBO 实现
        (void)ctx;
        m_Rotation += 0.01f;
    }

} // namespace Engine