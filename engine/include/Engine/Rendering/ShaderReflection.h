#pragma once

/**
 * @file ShaderReflection.h
 * @brief 着色器反射系统 — 自动提取 Uniforms/Samplers/UBO 的绑定点与布局
 *
 * 设计原则：
 *   - SPIRV-Cross 反射 GLSL/SPIR-V 着色器后，填充 ShaderReflectionData
 *   - 所有数据与具体着色语言解耦
 *   - 材质系统根据此数据自动生成 UBO 绑定点映射
 *
 * 集成方案：
 *   1. SpirVCompiler::CompileHLSL/CompileGLSL → SPIR-V binary
 *   2. spirv_cross::CompilerGLSL → 解析 SPIR-V 中的资源声明
 *   3. ExtractReflection(compiler) → ShaderReflectionData
 *   4. 材质系统使用 ShaderReflectionData 自动绑定
 */

#include "Engine/Types.h"
#include "Engine/Core/StringID.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Engine {
namespace Rendering {

    // ============================================================
    // Uniform Block 成员类型
    // ============================================================
    enum class UniformType : uint8 {
        Unknown = 0,
        Float, Float2, Float3, Float4,
        Int, Int2, Int3, Int4,
        UInt, UInt2, UInt3, UInt4,
        Mat3, Mat4,
        Bool,
    };

    inline const char* UniformTypeName(UniformType t) noexcept {
        switch (t) {
            case UniformType::Float:  return "float";
            case UniformType::Float2: return "vec2";
            case UniformType::Float3: return "vec3";
            case UniformType::Float4: return "vec4";
            case UniformType::Int:    return "int";
            case UniformType::Mat4:   return "mat4";
            default: return "unknown";
        }
    }

    // ============================================================
    // Uniform Block 成员描述
    // ============================================================
    /**
     * @brief UBO 内单个成员的元数据
     *
     * 由 SPIRV-Cross 反射生成，用于 CPU 端计算 UBO 成员在
     * std140 布局中的字节偏移与大小。
     */
    struct UniformMember {
        std::string name;
        UniformType type      = UniformType::Unknown;
        uint32_t    offset    = 0;     ///< 在 UBO 中的字节偏移
        uint32_t    size      = 0;     ///< 字节大小
        uint32_t    arraySize = 0;     ///< 0 = 非数组，N = 数组长度
    };

    // ============================================================
    // Uniform Block 描述
    // ============================================================
    /**
     * @brief 一个 UBO (Uniform Buffer Object) 的完整绑定信息
     */
    struct UniformBlock {
        std::string name;              ///< 着色器中的块名称
        uint32_t    binding = 0;       ///< 绑定槽位
        uint32_t    set     = 0;       ///< 描述符集（Vulkan）/ register space（D3D12）
        uint32_t    size    = 0;       ///< 块总大小（字节）
        std::vector<UniformMember> members;  ///< 块内成员列表
    };

    // ============================================================
    // Sampler 描述
    // ============================================================
    struct SamplerInfo {
        std::string name;
        uint32_t    binding = 0;
        uint32_t    set     = 0;
        int         dimension = 2;     ///< 2 = sampler2D, 3 = sampler3D, etc.
        bool        isArray   = false; ///< sampler2DArray
        bool        isShadow  = false; ///< sampler2DShadow
    };

    // ============================================================
    // Shader 反射完整数据
    // ============================================================
    /**
     * @brief 单个着色器阶段（Vertex/Fragment/Compute）的反射信息
     *
     * 由 SpirVCompiler::Compile() 后自动填充。
     */
    struct StageReflection {
        // ── Uniform Blocks (UBO) ──
        std::vector<UniformBlock> uniformBlocks;

        // ── Samplers ──
        std::vector<SamplerInfo> samplers;

        // ── Push Constants（Vulkan 特有, OpenGL 忽略） ──
        uint32_t pushConstantSize = 0;

        // ── 输入 / 输出 ──
        // Vertex shader 的输入属性布局
        struct InputAttribute {
            std::string name;
            uint32_t    location = 0;
            uint32_t    components = 4;  // 1=float, 2=vec2, 3=vec3, 4=vec4
        };
        std::vector<InputAttribute> inputAttributes;

        /// 便利查询：查找指定名称的 UBO
        const UniformBlock* FindUniformBlock(const std::string& name) const noexcept {
            for (const auto& ub : uniformBlocks)
                if (ub.name == name) return &ub;
            return nullptr;
        }

        /// 便利查询：查找指定名称的 Sampler 绑定槽位
        int32_t FindSamplerBinding(const std::string& name) const noexcept {
            for (const auto& s : samplers)
                if (s.name == name) return static_cast<int32_t>(s.binding);
            return -1;
        }
    };

    // ============================================================
    // Shader 反射聚合
    // ============================================================
    /**
     * @brief 完整 Shader 的反射信息（可能包含多个阶段）
     */
    struct ShaderReflectionData {
        StageReflection vertex;
        StageReflection fragment;
        StageReflection geometry;
        StageReflection compute;

        /** 获取所有阶段中所有 UBO 的并集 */
        std::vector<UniformBlock> GetAllUniformBlocks() const noexcept {
            std::vector<UniformBlock> result;
            auto appendUBs = [&](const StageReflection& stage) {
                for (const auto& ub : stage.uniformBlocks) result.push_back(ub);
            };
            appendUBs(vertex);
            appendUBs(fragment);
            appendUBs(geometry);
            appendUBs(compute);
            return result;
        }

        /** 获取所有阶段中所有 Sampler 的并集 */
        std::vector<SamplerInfo> GetAllSamplers() const noexcept {
            std::vector<SamplerInfo> result;
            auto appendSamplers = [&](const StageReflection& stage) {
                for (const auto& s : stage.samplers) result.push_back(s);
            };
            appendSamplers(vertex);
            appendSamplers(fragment);
            appendSamplers(geometry);
            appendSamplers(compute);
            return result;
        }
    };

    // ============================================================
    // SPIRV-Cross 反射接口
    // ============================================================
    /**
     * @brief 经 SPIRV-Cross 从 SPIR-V 二进制中提取反射信息
     *
     * @param spirvData      SPIR-V 二进制数据
     * @param wordCount      uint32_t 字形数
     * @param[out] outRefl   填充反射数据
     * @return true 表示成功
     *
     * 当前为空桩实现。需要链接 spirv-cross 库后激活。
     */
    bool ExtractShaderReflection(const uint32_t* spirvData,
                                 size_t wordCount,
                                 ShaderReflectionData& outRefl);

} // namespace Rendering
} // namespace Engine