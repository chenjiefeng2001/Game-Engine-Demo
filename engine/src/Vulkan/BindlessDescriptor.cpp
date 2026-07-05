/**
 * @file BindlessDescriptor.cpp
 * @brief Bindless Descriptor Indexing 分配器实现
 */

#include "Engine/Core/RHI/BindlessDescriptor.h"
#include <cstdlib>
#include <cstring>

namespace Engine {

BindlessAllocator::~BindlessAllocator() { Shutdown(); }

bool BindlessAllocator::Initialize(VkDevice device, uint32_t maxBindings) {
    if (m_Initialized) return true;
    m_Device = device;
    m_MaxBindings = maxBindings;

    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxBindings },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxBindings },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxBindings },
    };

    VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolCI.maxSets = maxBindings;
    poolCI.poolSizeCount = 3;
    poolCI.pPoolSizes = poolSizes;
    VkResult res = vkCreateDescriptorPool(device, &poolCI, nullptr, &m_Pool);
    if (res != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding bindings[] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxBindings, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxBindings, VK_SHADER_STAGE_ALL, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxBindings, VK_SHADER_STAGE_ALL, nullptr },
    };

    VkDescriptorBindingFlags bindingFlags[] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    bindingFlagsCI.bindingCount = 3;
    bindingFlagsCI.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutCI.pNext = &bindingFlagsCI;
    layoutCI.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutCI.bindingCount = 3;
    layoutCI.pBindings = bindings;
    res = vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &m_Layout);
    if (res != VK_SUCCESS) return false;

    VkDescriptorSetVariableDescriptorCountAllocateInfo variableCount{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
    variableCount.descriptorSetCount = 1;
    variableCount.pDescriptorCounts = &maxBindings;

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.pNext = &variableCount;
    allocInfo.descriptorPool = m_Pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_Layout;
    vkAllocateDescriptorSets(device, &allocInfo, &m_DescriptorSet);

    // PipelineLayout
    VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plCI.setLayoutCount = 1;
    plCI.pSetLayouts = &m_Layout;
    vkCreatePipelineLayout(device, &plCI, nullptr, &m_PipelineLayout);

    // 空闲索引池
    m_FreePool.capacity = maxBindings;
    m_FreePool.indices = static_cast<uint32_t*>(malloc(maxBindings * sizeof(uint32_t)));
    m_FreePool.count = 0;

    m_Initialized = true;
    return true;
}

void BindlessAllocator::Shutdown() {
    if (!m_Initialized || m_Device == VK_NULL_HANDLE) return;
    if (m_PipelineLayout) vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
    if (m_Layout) vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
    if (m_Pool) vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
    free(m_FreePool.indices);
    m_Initialized = false;
}

uint32_t BindlessAllocator::Allocate() {
    if (m_FreePool.count > 0) {
        return m_FreePool.indices[--m_FreePool.count];
    }
    return m_FreePool.nextFree.fetch_add(1, std::memory_order_relaxed);
}

void BindlessAllocator::Free(uint32_t index) {
    if (m_FreePool.count < m_FreePool.capacity) {
        m_FreePool.indices[m_FreePool.count++] = index;
    }
}

void BindlessAllocator::UpdateTexture(uint32_t index, VkImageView imageView, VkSampler sampler) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_DescriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
}

void BindlessAllocator::UpdateBuffer(uint32_t index, VkBuffer buffer, uint64_t offset, uint64_t range, VkDescriptorType type) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = range;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_DescriptorSet;
    write.dstBinding = (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? 0 :
                        (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) ? 1 : 2;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
}

} // namespace Engine