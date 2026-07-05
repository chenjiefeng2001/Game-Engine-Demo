#pragma once

/**
 * @file SpirVMeta.h
 * @brief SPIR-V 元数据缓存 — 着色器反射结果持久化
 *
 * 解决的核心问题：
 *   手动创建 VkPipelineLayout 和 DescriptorSetLayout 极易出错。
 *   通过 SPIRV-Cross 反射自动提取 Binding 映射并缓存到 .spv.meta 文件。
 *
 * 设计要点：
 *   - CompileAndReflect: 编译 GLSL → SPIR-V → 反射 Binding 信息
 *   - 元数据包含：UBO/SSBO/Sampler/PushConstant 的 Set/Binding/Size
 *   - VulkanPipelineLayoutCache 直接读取此元数据自动创建布局
 *   - 支持序列化/反序列化到二进制 .spv.meta 文件
 */

#include "Engine/Vulkan/VulkanCommon.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// SPIR-V 反射元数据
// ════════════════════════════════════════════════════════════

struct SpirVResourceBinding {
    uint32_t set{UINT32_MAX};       ///< Descriptor Set 索引
    uint32_t binding{UINT32_MAX};   ///< Binding 索引
    uint32_t size{0};               ///< 字节大小（UBO/SSBO）
    uint32_t arraySize{1};          ///< 数组元素数
    uint32_t stageFlags{0};         ///< VkShaderStageFlagBits

    enum class Type : uint8_t {
        UniformBuffer = 0,
        StorageBuffer = 1,
        SampledImage = 2,
        StorageImage = 3,
        PushConstant = 4,
    };
    Type type{Type::UniformBuffer};
};

struct SpirVMeta {
    std::vector<SpirVResourceBinding> bindings;     ///< 所有 Binding
    std::vector<uint32_t> pushConstantSizes;        ///< PushConstant 大小（按 stage）

    uint32_t vertexInputAttributes{0};              ///< 顶点输入属性数
    bool hasVertexShader{false};
    bool hasFragmentShader{false};
    bool hasComputeShader{false};

    /// 序列化到二进制流
    bool Serialize(const std::string& path) const {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        uint32_t count = static_cast<uint32_t>(bindings.size());
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (auto& b : bindings) {
            file.write(reinterpret_cast<const char*>(&b), sizeof(b));
        }
        uint32_t pcCount = static_cast<uint32_t>(pushConstantSizes.size());
        file.write(reinterpret_cast<const char*>(&pcCount), sizeof(pcCount));
        file.write(reinterpret_cast<const char*>(pushConstantSizes.data()),
                    pcCount * sizeof(uint32_t));
        return true;
    }

    /// 反序列化
    static SpirVMeta Deserialize(const std::string& path) {
        SpirVMeta meta;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return meta;

        uint32_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        meta.bindings.resize(count);
        if (count > 0)
            file.read(reinterpret_cast<char*>(meta.bindings.data()), count * sizeof(SpirVResourceBinding));

        uint32_t pcCount = 0;
        file.read(reinterpret_cast<char*>(&pcCount), sizeof(pcCount));
        meta.pushConstantSizes.resize(pcCount);
        if (pcCount > 0)
            file.read(reinterpret_cast<char*>(meta.pushConstantSizes.data()), pcCount * sizeof(uint32_t));
        return meta;
    }
};

} // namespace RHI
} // namespace Engine