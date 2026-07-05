/**
 * @file ShaderReflection.cpp
 * @brief SPIR-V 着色器反射实现 — 提取资源绑定信息
 *
 * 使用 SPIRV-Cross 库解析 SPIR-V 二进制数据。
 * 反射结果用于自动生成 Vulkan PipelineLayout 和 DescriptorSetLayout。
 */

#include "Engine/Core/RHI/ShaderReflection.h"
#include <spirv_cross/spirv_cross.hpp>

namespace Engine { namespace RHI {

ShaderReflectionData ReflectSPIRV(const uint32_t* spirv, size_t wordCount) {
    ShaderReflectionData data;

    if (!spirv || wordCount == 0) return data;

    try {
        spirv_cross::Compiler compiler(spirv, wordCount);
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        // ── Uniform Buffers (UBO) ──
        for (const auto& res : resources.uniform_buffers) {
            ShaderResourceBinding binding;
            binding.name        = compiler.get_name(res.id);
            binding.set         = compiler.get_decoration(res.id, spv::DecorationDescriptorSet);
            binding.binding     = compiler.get_decoration(res.id, spv::DecorationBinding);
            binding.type        = ShaderResourceType::UniformBuffer;
            binding.size        = static_cast<uint32_t>(compiler.get_declared_struct_size(
                                    compiler.get_type(res.base_type_id)));
            binding.arraySize   = 1;
            data.resources.push_back(binding);
        }

        // ── Storage Buffers (SSBO) ──
        for (const auto& res : resources.storage_buffers) {
            ShaderResourceBinding binding;
            binding.name        = compiler.get_name(res.id);
            binding.set         = compiler.get_decoration(res.id, spv::DecorationDescriptorSet);
            binding.binding     = compiler.get_decoration(res.id, spv::DecorationBinding);
            binding.type        = ShaderResourceType::StorageBuffer;
            binding.size        = static_cast<uint32_t>(compiler.get_declared_struct_size(
                                    compiler.get_type(res.base_type_id)));
            binding.arraySize   = 1;
            data.resources.push_back(binding);
        }

        // ── Sampled Images (纹理) ──
        for (const auto& res : resources.sampled_images) {
            ShaderResourceBinding binding;
            binding.name        = compiler.get_name(res.id);
            binding.set         = compiler.get_decoration(res.id, spv::DecorationDescriptorSet);
            binding.binding     = compiler.get_decoration(res.id, spv::DecorationBinding);
            binding.type        = ShaderResourceType::SampledImage;
            binding.size        = 0;
            binding.arraySize   = 1;
            data.resources.push_back(binding);
        }

        // ── Storage Images (UAV) ──
        for (const auto& res : resources.storage_images) {
            ShaderResourceBinding binding;
            binding.name        = compiler.get_name(res.id);
            binding.set         = compiler.get_decoration(res.id, spv::DecorationDescriptorSet);
            binding.binding     = compiler.get_decoration(res.id, spv::DecorationBinding);
            binding.type        = ShaderResourceType::StorageTexelBuffer;
            binding.size        = 0;
            binding.arraySize   = 1;
            data.resources.push_back(binding);
        }

        // ── Uniform Texel Buffers ──
        for (const auto& res : resources.uniform_texel_buffers) {
            ShaderResourceBinding binding;
            binding.name        = compiler.get_name(res.id);
            binding.set         = compiler.get_decoration(res.id, spv::DecorationDescriptorSet);
            binding.binding     = compiler.get_decoration(res.id, spv::DecorationBinding);
            binding.type        = ShaderResourceType::UniformTexelBuffer;
            binding.size        = 0;
            binding.arraySize   = 1;
            data.resources.push_back(binding);
        }

        // ── Push Constants ──
        for (const auto& res : resources.push_constant_buffers) {
            data.pushConstantSize = static_cast<uint32_t>(
                compiler.get_declared_struct_size(compiler.get_type(res.base_type_id)));
        }

    } catch (const std::exception& e) {
        // SPIRV-Cross 解析失败时返回空数据
        // 调用方应处理空 ShaderReflectionData 的情况
    }

    return data;
}

}} // namespace Engine::RHI