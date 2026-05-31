#pragma once
#include <string>
#include <vector>
#include "Engine/Core/Resources/Resource.h"
#include "Engine/Core/RenderResources/ShaderStage.h"

namespace Engine {

    /**
     * @brief 着色器基类 — 支持从简单 vertex+fragment 到多阶段管线
     *
     * 向后兼容：
     *   现有的 Shader(vert, frag) 构造函数仍可用，内部转换为两阶段。
     *   新的 CreateShaderFromStages() 支持任意阶段组合。
     *
     * 着色器阶段类型：
     *   - Vertex, Fragment（必需）
     *   - Geometry, TessControl, TessEvaluation（可选）
     *   - Compute（独立管线）
     *
     * 源码格式：
     *   - GLSL（当前，直接编译）
     *   - SPIR-V（未来，通过 shaderc 交叉编译）
     *   - HLSL（未来，通过 shaderc 转换到 SPIR-V）
     */
    class Shader : public Resource {
    public:
        Shader(std::string path)
            : Resource(std::move(path), ResourceType::Shader) {}
        virtual ~Shader() = default;

        // ════════════════════════════════════════════
        // 【旧接口】保持向后兼容 — 从文件加载 vertex + fragment
        // ════════════════════════════════════════════
        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        // ⚠️ RHI 原则：使用 float* 传递矩阵，彻底隔离第三方库
        virtual void SetMat4(const std::string& name, const float* data) = 0;
        virtual void SetVec3(const std::string& name, const float* data) = 0;
        virtual void SetVec4(const std::string& name, const float* data) = 0;
        virtual void SetFloat(const std::string& name, float value) = 0;
        virtual void SetInt(const std::string& name, int value) = 0;

        // ════════════════════════════════════════════
        // 【新接口】多阶段加载
        // ════════════════════════════════════════════

        /**
         * @brief 从多个着色器阶段加载（vs + fs + gs + tcs + tes + cs）
         * @param stages 阶段描述列表（顺序无关）
         * @return 是否成功
         */
        virtual bool LoadStages(const std::vector<ShaderStage>& stages) {
            (void)stages;
            return false;  // 默认不支持，子类覆盖
        }

        // ════════════════════════════════════════════
        // 【新接口】UBO / SSBO
        // ════════════════════════════════════════════

        virtual void BindUniformBlock(const std::string& name,
                                       uint64 bufferID,
                                       uint32 binding) {
            (void)name; (void)bufferID; (void)binding;
        }

        virtual void BindStorageBlock(const std::string& name,
                                       uint64 bufferID,
                                       uint32 binding) {
            (void)name; (void)bufferID; (void)binding;
        }

        // ════════════════════════════════════════════
        // 【新接口】着色器反射
        // ════════════════════════════════════════════

        virtual const ShaderReflection& GetReflection() const {
            static ShaderReflection empty;
            return empty;
        }

        // ════════════════════════════════════════════
        // 【新接口】查询
        // ════════════════════════════════════════════

        virtual uint32 GetStageCount() const { return 0; }
        virtual ShaderStageType GetStageType(uint32 index) const {
            (void)index;
            return ShaderStageType::Vertex;
        }
        virtual bool IsCompute() const { return false; }
        virtual uint64 GetNativeHandle() const { return 0; }
    };

    // ════════════════════════════════════════════════
    // SPIR-V 编译管道桩（未来扩展）
    // ════════════════════════════════════════════════

    /**
     * @brief SPIR-V 编译器接口
     *
     * 当前为空桩，需要 shaderc / glslang 库时激活。
     * 负责将 HLSL / GLSL 编译为 SPIR-V 二进制。
     */
    class SpirVCompiler {
    public:
        /// 编译结果
        struct Result {
            bool success = false;
            std::vector<uint32_t> spirv;   // SPIR-V 二进制
            std::string errorMsg;
        };

        /**
         * @brief 将 GLSL 源码编译为 SPIR-V
         */
        static Result CompileGLSL(const ShaderStage& stage) {
            (void)stage;
            // STUB: 需要集成 shaderc 库
            // 1. shaderc::Compiler compiler;
            // 2. auto result = compiler.CompileGlslToSpv(...)
            return Result{false, {}, "SpirVCompiler: shaderc not integrated yet"};
        }

        /**
         * @brief 将 HLSL 源码编译为 SPIR-V
         */
        static Result CompileHLSL(const ShaderStage& stage) {
            (void)stage;
            // STUB: 需要集成 shaderc 库
            // 1. shaderc::Compiler compiler;
            // 2. auto result = compiler.CompileGlslToSpv(... SHADERC_HLSL ...)
            return Result{false, {}, "SpirVCompiler: shaderc not integrated yet"};
        }

        /// 判断编译器是否可用
        static bool IsAvailable() { return false; }
    };

}