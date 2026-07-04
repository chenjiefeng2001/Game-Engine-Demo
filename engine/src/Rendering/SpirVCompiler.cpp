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

namespace {
    Engine::Logger s_Log("SpirV");
}

namespace Engine {
namespace Rendering {

    bool ExtractShaderReflection(const uint32_t* spirvData,
                                size_t wordCount,
                                ShaderReflectionData& outRefl)
    {
        if (!spirvData || wordCount == 0) {
            s_Log.Warn("ExtractShaderReflection: empty SPIR-V data");
            return false;
        }

        // ════════════════════════════════════════════════════════
        // 需要 spirv-cross 库后激活
        // ════════════════════════════════════════════════════════
        // #include <spirv_cross/spirv_cross.hpp>
        //
        // spirv_cross::CompilerGLSL comp(spirvData, wordCount);
        //
        // for (auto& resource : comp.get_shader_resources().uniform_buffers) {
        //     UniformBlock ub;
        //     ub.name = resource.name;
        //     ub.binding = comp.get_decoration(resource.id, spv::DecorationBinding);
        //     ub.set = comp.get_decoration(resource.id, spv::DecorationDescriptorSet);
        //     ub.size = (uint32_t)comp.get_declared_struct_size(
        //                   comp.get_type(resource.base_type_id));
        //     outRefl.vertex.uniformBlocks.push_back(ub);
        // }
        // for (auto& resource : comp.get_shader_resources().sampled_images) {
        //     SamplerInfo sampler;
        //     sampler.name = resource.name;
        //     sampler.binding = comp.get_decoration(resource.id, spv::DecorationBinding);
        //     sampler.set = comp.get_decoration(resource.id, spv::DecorationDescriptorSet);
        //     outRefl.fragment.samplers.push_back(sampler);
        // }
        // return true;
        // ════════════════════════════════════════════════════════

        s_Log.Warn("spirv-cross not integrated yet. Link third_party/spirv-cross to activate.");
        (void)outRefl;
        return false;
    }

}} // Engine::Rendering