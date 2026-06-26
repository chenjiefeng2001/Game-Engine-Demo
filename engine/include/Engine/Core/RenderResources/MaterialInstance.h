#pragma once

/**
 * @file MaterialInstance.h
 * @brief 材质实例 — 由 Shader Graph 生成的运行时材质
 *
 * 遵循 RHI 抽象工厂原则，所有 GPU 对象通过 IGraphicsFactory 创建。
 * 架构：
 *   ShaderGraph → HLSL 代码 → OpenGLShader 编译 → MaterialInstance
 *                                                        ↓
 *                                              MeshRenderer::Draw()
 *
 * 类似于 Unity 的 MaterialPropertyBlock / Unreal 的 MIDynamic，
 * 允许运行时修改参数（颜色、浮点、纹理）而不重新编译 shader。
 */

#include "Engine/Types.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/RenderResources/Texture.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <variant>
#include <glm/glm.hpp>

namespace Engine {

    // ═══════════════════════════════════════════════════════════════
    // 材质参数类型（类型安全的 variant）
    // ═══════════════════════════════════════════════════════════════
    struct MaterialParam {
        std::variant<
            float,          // Float
            glm::vec2,      // Float2
            glm::vec3,      // Float3
            glm::vec4,      // Color / Float4
            int,            // Int
            bool,           // Boolean
            std::string     // Texture path
        > value;

        MaterialParam() : value(0.0f) {}
        MaterialParam(float v) : value(v) {}
        MaterialParam(const glm::vec2& v) : value(v) {}
        MaterialParam(const glm::vec3& v) : value(v) {}
        MaterialParam(const glm::vec4& v) : value(v) {}
        MaterialParam(int v) : value(v) {}
        MaterialParam(bool v) : value(v) {}
        MaterialParam(const std::string& v) : value(v) {}
        MaterialParam(const char* v) : value(std::string(v)) {}
    };

    // ═══════════════════════════════════════════════════════════════
    // 材质实例 — 运行时材质，由 Shader Graph 生成
    // ═══════════════════════════════════════════════════════════════
    class MaterialInstance : public std::enable_shared_from_this<MaterialInstance> {
    public:
        /// 创建材质实例（通过 RHI 工厂）
        static std::shared_ptr<MaterialInstance> Create(IGraphicsFactory& factory);

        MaterialInstance(const MaterialInstance&) = delete;
        MaterialInstance& operator=(const MaterialInstance&) = delete;

        virtual ~MaterialInstance() = default;

        // ── 生命周期 ──

        /// 从 GLSL 源码编译 shader（替代 Shader Graph 生成的代码）
        bool CompileFromGLSL(const std::string& vertexSource,
                             const std::string& fragmentSource);

        /// 从 Shader Graph 输出代码编译（自动使用内置 vertex shader）
        bool CompileFromShaderGraph(const std::string& fragmentSource);

        /// 标记为脏，下次绘制时重编译
        void SetDirty() { m_IsDirty = true; }

        // ── 参数访问 ──

        /// 设置浮点参数
        void SetFloat(const std::string& name, float value) { m_Parameters[name] = MaterialParam(value); m_IsDirty = true; }

        /// 设置向量参数
        void SetVector2(const std::string& name, const glm::vec2& value) { m_Parameters[name] = MaterialParam(value); m_IsDirty = true; }
        void SetVector3(const std::string& name, const glm::vec3& value) { m_Parameters[name] = MaterialParam(value); m_IsDirty = true; }
        void SetVector4(const std::string& name, const glm::vec4& value) { m_Parameters[name] = MaterialParam(value); m_IsDirty = true; }

        /// 设置颜色
        void SetColor(const std::string& name, const glm::vec4& color) { m_Parameters[name] = MaterialParam(color); m_IsDirty = true; }

        /// 设置纹理
        void SetTexture(const std::string& name, const std::string& path) { m_Parameters[name] = MaterialParam(path); m_IsDirty = true; }

        /// 获取参数值
        MaterialParam GetParam(const std::string& name) const {
            auto it = m_Parameters.find(name);
            return it != m_Parameters.end() ? it->second : MaterialParam(0.0f);
        }

        // ── 绑定与绘制 ──

        /// 绑定材质到渲染上下文（设置 shader + uniforms）
        virtual void Bind(IRenderContext& ctx);

        /// 解绑
        virtual void Unbind();

        // ── 查询 ──

        /// 是否已就绪（shader 已编译）
        bool IsReady() const { return m_Shader != nullptr; }

        /// 获取编译错误日志
        const std::string& GetCompileLog() const { return m_CompileLog; }

        /// 获取 shader 指针
        std::shared_ptr<Shader> GetShader() const { return m_Shader; }

        /// 获取所有参数
        const std::unordered_map<std::string, MaterialParam>& GetParameters() const { return m_Parameters; }

    protected:
        /// 构造函数（通过 Create() 调用）
        MaterialInstance(IGraphicsFactory& factory) : m_Factory(factory) {}

        /// 应用所有参数到当前绑定的 shader
        virtual void ApplyParameters();

        IGraphicsFactory& m_Factory;
        std::shared_ptr<Shader> m_Shader;
        std::string m_VertexSrc;
        std::string m_FragmentSrc;
        std::string m_CompileLog;
        std::unordered_map<std::string, MaterialParam> m_Parameters;
        bool m_IsDirty = true;
    };

    // ═══════════════════════════════════════════════════════════════
    // 材质预览场景 — 在 Viewport 中预览材质效果
    // ═══════════════════════════════════════════════════════════════
    class MaterialPreview {
    public:
        MaterialPreview();
        ~MaterialPreview() = default;

        /// 初始化预览资源（球体 mesh + light 环境）
        void Init(IGraphicsFactory& factory);

        /// 设置要预览的材质
        void SetMaterial(std::shared_ptr<MaterialInstance> material) { m_Material = material; }

        /// 获取当前预览材质
        std::shared_ptr<MaterialInstance> GetMaterial() const { return m_Material; }

        /// 渲染预览（在指定 FBO 中绘制）
        void Render(IRenderContext& ctx);

        /// 设置环境光颜色
        void SetEnvironmentColor(const glm::vec3& color) { m_EnvColor = color; }

        /// 设置旋转角度
        void SetRotation(float angle) { m_Rotation = angle; }

    private:
        std::shared_ptr<MaterialInstance> m_Material;
        std::shared_ptr<Shader> m_LightShader;  // 环境光 shading
        std::shared_ptr<VertexArray> m_SphereVAO;
        std::shared_ptr<VertexArray> m_CubeVAO;
        glm::vec3 m_EnvColor = glm::vec3(0.2f, 0.2f, 0.3f);
        float m_Rotation = 0.0f;
        float m_Zoom = 3.5f;
        bool m_Initialized = false;
    };

} // namespace Engine