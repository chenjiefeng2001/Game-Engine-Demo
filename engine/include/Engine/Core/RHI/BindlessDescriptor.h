#pragma once

/**
 * @file BindlessDescriptor.h
 * @brief Bindless Descriptor Indexing 分配器
 *
 * 利用 VK_EXT_descriptor_indexing 实现无绑定点限制的纹理/Buffer 访问。
 * 着色器中通过 ResourceDescriptorHeap[index] 直接访问。
 */

#include "Engine/Types.h"
#include <vulkan/vulkan.h>

namespace Engine {

class BindlessAllocator {
public:
    BindlessAllocator() = default;
    ~BindlessAllocator();

    BindlessAllocator(const BindlessAllocator&) = delete;
    BindlessAllocator& operator=(const BindlessAllocator&) = delete;

    bool Initialize(VkDevice device, uint32_t maxBindings = 4096);
    void Shutdown();

    /** 分配一个描述符索引（返回 index） */
    uint32_t Allocate();

    /** 释放描述符索引 */
    void Free(uint32_t index);

    /** 更新纹理描述符（写 SRV） */
    void UpdateTexture(uint32_t index, VkImageView imageView, VkSampler sampler);

    /** 更新 Buffer 描述符（写 CBV/SRV/UAV） */
    void UpdateBuffer(uint32_t index, VkBuffer buffer, uint64_t offset, uint64_t range, VkDescriptorType type);

    /** 获取描述符集（绑定到命令列表时使用） */
    VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }
    VkDescriptorSetLayout GetLayout() const { return m_Layout; }
    VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }

    bool IsValid() const { return m_Initialized; }

private:
    bool m_Initialized = false;
    VkDevice m_Device = VK_NULL_HANDLE;

    VkDescriptorPool      m_Pool{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_Layout{VK_NULL_HANDLE};
    VkDescriptorSet       m_DescriptorSet{VK_NULL_HANDLE};
    VkPipelineLayout      m_PipelineLayout{VK_NULL_HANDLE};

    // 空闲索引池
    struct alignas(64) {
        uint32_t* indices = nullptr;
        uint32_t count = 0;
        uint32_t capacity = 0;
        std::atomic<uint32_t> nextFree{1};
    } m_FreePool;

    uint32_t m_MaxBindings = 0;
};

} // namespace Engine