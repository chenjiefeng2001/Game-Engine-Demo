/**
 * @file VulkanPipelineLayoutCache.cpp
 * @brief Pipeline Layout 缓存实现（SPIRV-Cross 反射）
 */

#include "Engine/Vulkan/VulkanPipelineLayoutCache.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"

#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include <cstring>
#include <vector>

namespace Engine {
namespace RHI {

VulkanPipelineLayoutCache::VulkanPipelineLayoutCache(VulkanDevice* device) noexcept
    : m_Device(device) {}

VulkanPipelineLayoutCache::~VulkanPipelineLayoutCache() {
    Clear();
}

VkPipelineLayout VulkanPipelineLayoutCache::GetOrCreateLayout(
    const uint32_t* spirvData, size_t spirvSize)
{
    if (!spirvData || spirvSize == 0) {
        return VK_NULL_HANDLE;
    }

    // 计算哈希
    LayoutKey key{};
    key.hash1 = 0;
    key.hash2 = 0;
    // 简化哈希：从 SPIR-V 二进制数据算两个 64 位
    const size_t wordCount = spirvSize / sizeof(uint32_t);
    for (size_t i = 0; i < wordCount; ++i) {
        key.hash1 ^= spirvData[i] + 0x9e3779b9 + (key.hash1 << 6) + (key.hash1 >> 2);
        key.hash2 ^= spirvData[i] * 0x9e3779b9 + (key.hash2 << 11) + (key.hash2 >> 7);
    }

    // 查缓存
    auto it = m_Cache.find(key);
    if (it != m_Cache.end()) {
        return it->second;
    }

    // SPIRV-Cross 反射
    VkDevice vkDevice = m_Device->GetVkDevice();

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkPushConstantRange> pushConstants;
    std::vector<VkDescriptorSetLayout> setLayouts;
    bool hasBindings = false;

    try {
        spirv_cross::Compiler comp(spirvData, spirvSize / sizeof(uint32_t));

        // 提取 UBO 信息
        auto uboResources = comp.get_shader_resources().uniform_buffers;
        for (auto& ub : uboResources) {
            hasBindings = true;
            const auto& type = comp.get_type(ub.type_id);
            unsigned binding = comp.get_decoration(ub.id, spv::DecorationBinding);
            unsigned descriptorSet = comp.get_decoration(ub.id, spv::DecorationDescriptorSet);

            VkDescriptorSetLayoutBinding vb{};
            vb.binding = binding;
            vb.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            vb.descriptorCount = type.array.empty() ? 1 : type.array[0];
            vb.stageFlags = VK_SHADER_STAGE_ALL;
            bindings.push_back(vb);
        }

        // 提取 SampledImage 信息
        auto samplerResources = comp.get_shader_resources().sampled_images;
        for (auto& si : samplerResources) {
            hasBindings = true;
            unsigned binding = comp.get_decoration(si.id, spv::DecorationBinding);
            unsigned descriptorSet = comp.get_decoration(si.id, spv::DecorationDescriptorSet);

            VkDescriptorSetLayoutBinding vb{};
            vb.binding = binding;
            vb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            vb.descriptorCount = 1;
            vb.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings.push_back(vb);
        }

        // 提取 SSBO 信息
        auto ssboResources = comp.get_shader_resources().storage_buffers;
        for (auto& sb : ssboResources) {
            hasBindings = true;
            unsigned binding = comp.get_decoration(sb.id, spv::DecorationBinding);
            unsigned descriptorSet = comp.get_decoration(sb.id, spv::DecorationDescriptorSet);

            VkDescriptorSetLayoutBinding vb{};
            vb.binding = binding;
            vb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            vb.descriptorCount = 1;
            vb.stageFlags = VK_SHADER_STAGE_ALL;
            bindings.push_back(vb);
        }

        // 提取 PushConstant 信息
        auto pushConstantResources = comp.get_shader_resources().push_constant_buffers;
        if (!pushConstantResources.empty()) {
            for (auto& pc : pushConstantResources) {
                const auto& type = comp.get_type(pc.type_id);
                unsigned offset = comp.get_decoration(pc.id, spv::DecorationOffset);
                unsigned size = comp.get_declared_struct_size(type);

                VkPushConstantRange range{};
                range.stageFlags = VK_SHADER_STAGE_ALL;
                range.offset = offset;
                range.size = size;
                pushConstants.push_back(range);
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[PipelineLayoutCache] SPIRV-Cross error: %s\n", e.what());
    }

    // 创建 DescriptorSetLayout
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    if (hasBindings && !bindings.empty()) {
        // 启用 DescriptorIndexing 的多绑定布局
        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlags{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT};

        std::vector<VkDescriptorBindingFlags> flags(bindings.size(),
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);

        bindingFlags.bindingCount = (uint32_t)bindings.size();
        bindingFlags.pBindingFlags = flags.data();

        VkDescriptorSetLayoutCreateInfo layoutCI{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutCI.pNext = &bindingFlags;
        layoutCI.bindingCount = (uint32_t)bindings.size();
        layoutCI.pBindings = bindings.data();

        VkResult res = vkCreateDescriptorSetLayout(vkDevice, &layoutCI, nullptr, &setLayout);
        if (res != VK_SUCCESS) {
            std::fprintf(stderr, "[PipelineLayoutCache] Failed to create descriptor set layout\n");
        }
    }

    // 创建 PipelineLayout
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    if (setLayout != VK_NULL_HANDLE) {
        setLayouts.push_back(setLayout);
        plCI.setLayoutCount = 1;
        plCI.pSetLayouts = &setLayout;
    }
    if (!pushConstants.empty()) {
        plCI.pushConstantRangeCount = (uint32_t)pushConstants.size();
        plCI.pPushConstantRanges = pushConstants.data();
    }

    VkResult res = vkCreatePipelineLayout(vkDevice, &plCI, nullptr, &pipelineLayout);
    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "[PipelineLayoutCache] Failed to create pipeline layout\n");
        if (setLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(vkDevice, setLayout, nullptr);
        }
        return VK_NULL_HANDLE;
    }

    // 缓存
    m_Cache[key] = pipelineLayout;

    return pipelineLayout;
}

void VulkanPipelineLayoutCache::Clear() {
    VkDevice vkDevice = m_Device->GetVkDevice();
    for (auto& [key, layout] : m_Cache) {
        if (layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(vkDevice, layout, nullptr);
        }
    }
    m_Cache.clear();
}

} // namespace RHI
} // namespace Engine