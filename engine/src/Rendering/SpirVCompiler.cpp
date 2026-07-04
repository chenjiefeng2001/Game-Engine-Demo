/**
 * @file SpirVCompiler.cpp
 * @brief SPIR-V 编译管道 — 编译缓存 + 反射接口
 *
 * SpirVCompiler::CompileGLSL / CompileHLSL / IsAvailable 已在
 * Shader.h 中作为内联空桩定义。此文件仅补充：
 *   1. FNV-1a 编译缓存键生成
 *   2. ExtractShaderReflection() (SPIRV-Cross 反射接口)
 */

#include "Engine/Rendering/ShaderReflection.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/RenderResources/ShaderStage.h"  // for ShaderStageType enum

#ifdef ENGINE_HAS_SPIRV_CROSS
// 使用 third_party/spirv-cross/ 中的版本 (非 Vulkan SDK 内置版本)
// CompilerGLSL 定义在 spirv_glsl.hpp 而非 spirv_cross.hpp 中
#include "spirv_glsl.hpp"
#include "spirv_cross_util.hpp"
#endif

namespace {
    Engine::Logger s_Log("SpirV");
}

namespace Engine {
namespace Rendering {

#ifdef ENGINE_HAS_SPIRV_CROSS

    // ════════════════════════════════════════════════════════
    // 内部：从 SPIRV-Cross 提取单个阶段的反射信息
    // ════════════════════════════════════════════════════════
    static bool ReflectStage(const spirv_cross::CompilerGLSL& comp,
                             ShaderStageType stageType,
                             StageReflection& outStage)
    {
        auto resources = comp.get_shader_resources();

        // ── Uniform Blocks (UBO) ──
        for (auto& resource : resources.uniform_buffers) {
            UniformBlock ub;
            ub.name    = resource.name;
            ub.binding = comp.get_decoration(resource.id, spv::DecorationBinding);
            ub.set     = comp.get_decoration(resource.id, spv::DecorationDescriptorSet);
            ub.size    = static_cast<uint32_t>(
                comp.get_declared_struct_size(comp.get_type(resource.base_type_id)));

            // 提取成员信息
            const auto& type = comp.get_type(resource.base_type_id);
            for (uint32_t i = 0; i < type.member_types.size(); ++i) {
                UniformMember member;
                member.name = comp.get_member_name(resource.base_type_id, i);
                member.offset = static_cast<uint32_t>(
                    comp.type_struct_member_offset(type, i));
                member.size   = static_cast<uint32_t>(
                    comp.get_declared_struct_member_size(type, i));
                member.arraySize = type.array_size_literal[i]
                    ? type.array[i] : 0;

                // 类型映射
                auto spirvType = comp.get_type(type.member_types[i]).basetype;
                switch (spirvType) {
                    case spirv_cross::SPIRType::Float:
                        member.type = (type.member_types[i] < 4)
                            ? static_cast<UniformType>(static_cast<int>(UniformType::Float) + type.vecsize - 1)
                            : UniformType::Float4;
                        break;
                    case spirv_cross::SPIRType::Int:
                        member.type = UniformType::Int;
                        break;
                    case spirv_cross::SPIRType::UInt:
                        member.type = UniformType::UInt;
                        break;
                    case spirv_cross::SPIRType::Boolean:
                        member.type = UniformType::Bool;
                        break;
                    default:
                        member.type = UniformType::Unknown;
                        break;
                }

                ub.members.push_back(member);
            }

            outStage.uniformBlocks.push_back(std::move(ub));
        }

        // ── Samplers / Sampled Images ──
        for (auto& resource : resources.sampled_images) {
            SamplerInfo sampler;
            sampler.name     = resource.name;
            sampler.binding  = comp.get_decoration(resource.id, spv::DecorationBinding);
            sampler.set      = comp.get_decoration(resource.id, spv::DecorationDescriptorSet);

            const auto& type = comp.get_type(resource.base_type_id);
            sampler.dimension = type.image.dim;
            sampler.isArray   = type.array_size_literal[0] && type.array[0] > 0;
            sampler.isShadow  = type.image.depth;

            outStage.samplers.push_back(std::move(sampler));
        }

        // ── Storage Buffers (SSBO) ──
        for (auto& resource : resources.storage_buffers) {
            UniformBlock ub;
            ub.name    = resource.name;
            ub.binding = comp.get_decoration(resource.id, spv::DecorationBinding);
            ub.set     = comp.get_decoration(resource.id, spv::DecorationDescriptorSet);
            ub.size    = static_cast<uint32_t>(
                comp.get_declared_struct_size(comp.get_type(resource.base_type_id)));
            outStage.uniformBlocks.push_back(std::move(ub));
        }

        // ── Push Constants ──
        if (!resources.push_constant_buffers.empty()) {
            const auto& pc = resources.push_constant_buffers[0];
            outStage.pushConstantSize = static_cast<uint32_t>(
                comp.get_declared_struct_size(comp.get_type(pc.base_type_id)));
        }

        // ── Vertex Input Attributes ──
        if (stageType == ShaderStageType::Vertex) {
            for (auto& resource : resources.stage_inputs) {
                StageReflection::InputAttribute attr;
                attr.name       = resource.name;
                attr.location   = comp.get_decoration(resource.id, spv::DecorationLocation);
                attr.components = comp.get_type(resource.base_type_id).vecsize;
                outStage.inputAttributes.push_back(std::move(attr));
            }
        }

        return true;
    }

#endif // ENGINE_HAS_SPIRV_CROSS

    // ════════════════════════════════════════════════════════
    // ExtractShaderReflection — 主入口
    // ════════════════════════════════════════════════════════
    bool ExtractShaderReflection(const uint32_t* spirvData,
                                 size_t wordCount,
                                 ShaderReflectionData& outRefl)
    {
        if (!spirvData || wordCount == 0) {
            s_Log.Warn("ExtractShaderReflection: empty SPIR-V data");
            return false;
        }

#ifdef ENGINE_HAS_SPIRV_CROSS
        try {
            spirv_cross::CompilerGLSL comp(spirvData, wordCount);

            // 自动推断此 SPIR-V 对应的着色器阶段
            spirv_cross::ShaderResources resources = comp.get_shader_resources();

            // 判断阶段：根据是否存在 stage_inputs / stage_outputs 推断
            bool hasVertexInputs   = !resources.stage_inputs.empty();
            bool hasFragmentOutput = !resources.stage_outputs.empty();
            // compute shader 有 storage buffers/images 但没有 inputs/outputs
            bool hasComputeResources = !resources.storage_buffers.empty() ||
                                       !resources.storage_images.empty();

            ShaderStageType stageType = ShaderStageType::Fragment;
            if (hasVertexInputs && !hasFragmentOutput) {
                stageType = ShaderStageType::Vertex;
            } else if (hasFragmentOutput) {
                stageType = ShaderStageType::Fragment;
            } else if (hasComputeResources) {
                stageType = ShaderStageType::Compute;
            } else {
                // fallback: 根据是否有 uniform buffers 猜测
                stageType = !resources.uniform_buffers.empty()
                    ? ShaderStageType::Fragment
                    : ShaderStageType::Compute;
            }

            // 提取到对应的阶段
            StageReflection stage;
            if (!ReflectStage(comp, stageType, stage)) {
                s_Log.Error("ReflectStage failed for stage type {}", static_cast<int>(stageType));
                return false;
            }

            // 写入对应的阶段输出
            switch (stageType) {
                case ShaderStageType::Vertex:
                    outRefl.vertex = std::move(stage);
                    break;
                case ShaderStageType::Fragment:
                    outRefl.fragment = std::move(stage);
                    break;
                case ShaderStageType::Compute:
                    outRefl.compute = std::move(stage);
                    break;
                default:
                    // 对于几何/细分着色器，暂时放入 vertex 作为 fallback
                    outRefl.vertex = std::move(stage);
                    break;
            }

            s_Log.Info("Shader reflection extracted: {} UBOs, {} samplers, {} inputs",
                       stage.uniformBlocks.size(),
                       stage.samplers.size(),
                       stage.inputAttributes.size());

            return true;
        }
        catch (const spirv_cross::CompilerError& e) {
            s_Log.Error("SPIRV-Cross error: {}", e.what());
            return false;
        }
        catch (const std::exception& e) {
            s_Log.Error("Unexpected error during reflection: {}", e.what());
            return false;
        }
#else
        s_Log.Warn("spirv-cross not integrated yet. Link third_party/spirv-cross to activate.");
        (void)outRefl;
        return false;
#endif
    }

    // ════════════════════════════════════════════════════════
    // FNV-1a 编译缓存键生成（用于 Shader 变体缓存）
    // ════════════════════════════════════════════════════════
    uint64_t ComputeShaderCacheKey(const std::string& source,
                                   ShaderStageType stage,
                                   const std::string& macros)
    {
        // FNV-1a 64-bit hash
        constexpr uint64_t kFNVOffsetBasis = 14695981039346656037ULL;
        constexpr uint64_t kFNVPrime       = 1099511628211ULL;

        uint64_t hash = kFNVOffsetBasis;
        auto update = [&](const std::string& data) {
            for (char c : data) {
                hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
                hash *= kFNVPrime;
            }
        };

        update(source);
        update(macros);
        update(std::to_string(static_cast<int>(stage)));

        return hash;
    }

}} // Engine::Rendering