#pragma once

/**
 * @file VulkanPipelineLayoutCache.h
 * @brief Pipeline Layout 自动推导缓存 — 从 SPIRV-Cross 反射自动生成布局
 *
 * 设计要点：
 *   - 使用 SPIRV-Cross 反射提取 UBO/Sampler/PushConstant 绑定信息
 *   - 自动生成 VkDescriptorSetLayout + VkPipelineLayout
 *   - 缓存已计算的布局，避免重复创建
 */

#include "Engine/Vulkan/VulkanCommon.h"
#include <unordered_map>

namespace Engine {
namespace RHI {

/**
 * @brief Pipeline Layout 缓存
 *
 * 使用方式：
 * @code
 *   VulkanPipelineLayoutCache cache(device);
 *   VkPipelineLayout layout = cache.GetOrCreateLayout(spirvData, spirvSize);
 * @endcode
 */
class VulkanPipelineLayoutCache {
public:
    explicit VulkanPipelineLayoutCache(VulkanDevice* device) noexcept;
    ~VulkanPipelineLayoutCache();

    VulkanPipelineLayoutCache(const VulkanPipelineLayoutCache&) = delete;
    VulkanPipelineLayoutCache& operator=(const VulkanPipelineLayoutCache&) = delete;

    /**
     * @brief 获取或创建 Pipeline Layout
     *
     * @param spirvData SPIR-V 二进制数据指针
     * @param spirvSize SPIR-V 数据大小（字节）
     * @return VkPipelineLayout（缓存命中则直接返回已有布局）
     */
    VkPipelineLayout GetOrCreateLayout(const uint32_t* spirvData, size_t spirvSize);

    /**
     * @brief 清除缓存（在设备销毁时调用）
     */
    void Clear();

private:
    struct LayoutKey {
        uint64_t hash1;
        uint64_t hash2;

        bool operator==(const LayoutKey& o) const noexcept {
            return hash1 == o.hash1 && hash2 == o.hash2;
        }
    };

    struct LayoutKeyHasher {
        size_t operator()(const LayoutKey& k) const noexcept {
            return k.hash1 ^ (k.hash2 * 0x9e3779b9);
        }
    };

    VulkanDevice* m_Device;
    std::unordered_map<LayoutKey, VkPipelineLayout, LayoutKeyHasher> m_Cache;
};

} // namespace RHI
} // namespace Engine