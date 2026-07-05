#pragma once

/**
 * @file ShaderReflection.h
 * @brief SPIR-V 着色器反射数据结构
 *
 * 从 SPIR-V 二进制数据提取着色器的资源绑定信息，
 * 用于自动生成 PipelineLayout 和 DescriptorSetLayout。
 * 取代手动匹配 set/binding 的硬编码方式。
 */

#include "Engine/Types.h"
#include <string>
#include <vector>

namespace Engine { namespace RHI {

enum class ShaderResourceType : uint8 {
    UniformBuffer,       // UBO — 常量缓冲
    SampledImage,        // 组合纹理采样器
    StorageBuffer,       // SSBO — 可读写缓冲
    UniformTexelBuffer,  // 统一纹素缓冲
    StorageTexelBuffer,  // 存储纹素缓冲（UAV）
    PushConstant,        // 推送常量
};

struct ShaderResourceBinding {
    std::string name;
    ShaderResourceType type;
    uint32_t set;        // Descriptor Set 索引
    uint32_t binding;    // Binding 槽位
    uint32_t size;       // UBO/SSBO 大小（字节）
    uint32_t arraySize;  // 数组大小（1 = 非数组）
};

struct ShaderReflectionData {
    std::vector<ShaderResourceBinding> resources;
    uint32_t pushConstantSize = 0;  // Push Constant 总大小

    /** 查找指定 set/binding 的资源 */
    const ShaderResourceBinding* Find(uint32_t set, uint32_t binding) const {
        for (auto& r : resources) {
            if (r.set == set && r.binding == binding) return &r;
        }
        return nullptr;
    }

    /** 是否包含计算着色器专用的资源 */
    bool HasComputeResources() const {
        for (auto& r : resources) {
            if (r.type == ShaderResourceType::StorageBuffer) return true;
        }
        return false;
    }
};

/**
 * @brief 从 SPIR-V 二进制数据提取反射信息
 * @param spirv SPIR-V 字节码指针
 * @param wordCount 字数（非字节数）
 * @return 解析后的反射数据
 */
ShaderReflectionData ReflectSPIRV(const uint32_t* spirv, size_t wordCount);

}} // namespace Engine::RHI