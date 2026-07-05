#pragma once

#include "Engine/Core/Resources/Resource.h"
#include "Engine/Core/RenderResources/ShaderStage.h"
#include <vector>
#include <string>

namespace Engine {

    /**
     * @brief 着色器程序 — 多阶段着色器的抽象接口
     *
     * 这是 Shader 的升级版本，支持：
     *   - 任意数量的着色器阶段（VS/FS/GS/TCS/TES/CS）
     *   - 多种源码格式（GLSL / SPIR-V / HLSL）
     *   - 着色器反射（uniform/SSBO 查询）
     *   - 热加载
     *   - SPIR-V 交叉编译（未来通过 shaderc）
     *
     * 向后兼容：
     *   旧的 Shader 类保留并继承自 ShaderProgram，
     *   现有的 Shader(vert, frag) 构造函数自动转换为两阶段 ShaderProgram。
     */
    class ShaderProgram : public Resource {
    public:
        ShaderProgram(const std::string& name)
            : Resource(name, ResourceType::Shader) {}
        virtual ~ShaderProgram() = default;

        // ════════════════════════════════════════════
        // 生命周期
        // ════════════════════════════════════════════

        /**
         * @brief 从多个着色器阶段加载并编译
         * @param stages 着色器阶段描述列表
         * @return 是否成功
         */
        virtual bool LoadStages(const std::vector<ShaderStage>& stages) = 0;

        /**
         * @brief 从 GLSL 源码文件加载 vertex+fragment（向后兼容）
         * @param vertexPath   顶点着色器文件路径
         * @param fragmentPath 片段着色器文件路径
         * @return 是否成功
         */
        virtual bool LoadFromFiles(const std::string& vertexPath,
                                   const std::string& fragmentPath) = 0;

        // ════════════════════════════════════════════
        // 绑定 / 设置 uniform
        // ════════════════════════════════════════════

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        virtual void SetMat4(const std::string& name, const float* data) = 0;
        virtual void SetVec3(const std::string& name, const float* data) = 0;
        virtual void SetVec4(const std::string& name, const float* data) = 0;
        virtual void SetFloat(const std::string& name, float value) = 0;
        virtual void SetInt(const std::string& name, int value) = 0;

        // ════════════════════════════════════════════
        // Uniform Buffer Object (UBO) / Shader Storage Buffer (SSBO)
        // ════════════════════════════════════════════

        /**
         * @brief 绑定 Uniform Buffer 到指定 binding 点
         * @param name      uniform block 名称
         * @param bufferID  平台相关的 buffer handle
         * @param binding   binding 索引
         */
        virtual void BindUniformBlock(const std::string& name,
                                       uint64 bufferID,
                                       uint32 binding) = 0;

        /**
         * @brief 绑定 Shader Storage Buffer 到指定 binding 点
         */
        virtual void BindStorageBlock(const std::string& name,
                                       uint64 bufferID,
                                       uint32 binding) = 0;

        // ════════════════════════════════════════════
        // 反射
        // ════════════════════════════════════════════

        /**
         * @brief 获取着色器反射信息（编译后调用）
         */
        virtual const ShaderReflection& GetReflection() const = 0;

        // ════════════════════════════════════════════
        // 热加载
        // ════════════════════════════════════════════

        /**
         * @brief 重新加载所有阶段
         * @return 是否成功
         */
        virtual bool Reload() override = 0;

        // ════════════════════════════════════════════
        // 查询
        // ════════════════════════════════════════════

        virtual uint32 GetStageCount() const = 0;
        virtual ShaderStageType GetStageType(uint32 index) const = 0;
        virtual bool IsCompute() const = 0;
        virtual uint64 GetNativeHandle() const = 0;
    };

} // namespace Engine
