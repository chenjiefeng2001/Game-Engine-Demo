#pragma once

/**
 * @file DescriptorHeap.h
 * @brief 全局描述符堆 — D3D12 Shader-Visible Heap + Vulkan Descriptor Pool
 *
 * 统一管理 GPU 资源的描述符分配，支持 Bindless 模式。
 * 所有纹理/Buffer 创建时分配固定 Index，运行时直接通过 Index 访问。
 */

#include "Engine/Types.h"
#include <vector>
#include <mutex>

namespace Engine { namespace RHI {

class DescriptorHeapAllocator {
public:
    DescriptorHeapAllocator() = default;
    ~DescriptorHeapAllocator();

    bool Initialize(uint32_t maxDescriptors = 65536);
    void Shutdown();

    /** 分配一个堆索引 */
    uint32_t AllocateIndex();

    /** 释放堆索引 */
    void FreeIndex(uint32_t index);

    // ── D3D12 特有 ── 
    void* GetD3D12Heap() const { return m_D3D12Heap; }
    uint32_t GetDescriptorSize() const { return m_DescriptorSize; }

    // ── Vulkan 特有 ──
    void* GetVkDescriptorPool() const { return m_VkPool; }
    void* GetVkDescriptorSetLayout() const { return m_VkLayout; }

private:
    bool m_Initialized = false;
    uint32_t m_MaxDescriptors = 0;
    uint32_t m_DescriptorSize = 0;
    std::vector<uint32_t> m_FreeList;
    uint32_t m_NextIndex = 1;
    std::mutex m_Mutex;

    // D3D12
    void* m_D3D12Heap = nullptr;

    // Vulkan
    void* m_VkPool = nullptr;
    void* m_VkLayout = nullptr;
};

}} // namespace Engine::RHI