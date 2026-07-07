/**
 * @file StagingBufferManager.cpp
 * @brief 全局环形上传缓冲管理器实现
 */

#include "Engine/RHI/StagingBufferManager.h"

namespace Engine { namespace RHI {

StagingBufferManager::~StagingBufferManager() { Shutdown(); }

bool StagingBufferManager::Initialize(IRHIDevice& device, uint64_t bufferSize) {
    if (m_Initialized) Shutdown();

    m_BufferSize = bufferSize;

    RHIBufferDesc desc;
    desc.size = m_BufferSize;
    desc.stride = 0;
    desc.memoryUsage = MemoryUsage::CPU_To_GPU;
    desc.initialData = nullptr;

    auto buffer = device.CreateBuffer(desc);
    if (!buffer) {
        Logger("StagingBuffer").Error("Failed to create staging buffer (size={})", m_BufferSize);
        return false;
    }

    m_StagingBuffer = buffer.get();

    // 持久映射
    const auto& alloc = m_StagingBuffer->GetAllocation();
    if (alloc.mappedPtr) {
        m_MappedPtr = const_cast<void*>(alloc.mappedPtr);
    }

    m_CurrentOffset = 0;
    m_Initialized = true;
    Logger("StagingBuffer").Info("StagingBufferManager initialized: {} MB", m_BufferSize / (1024 * 1024));
    return true;
}

StagingAllocation StagingBufferManager::Allocate(uint64_t size, uint64_t alignment) {
    if (!m_Initialized || !m_StagingBuffer || !m_MappedPtr) return {};

    // 对齐
    uint64_t aligned = (size + alignment - 1) & ~(alignment - 1);

    // 环形处理：若空间不够则绕回开头
    if (m_CurrentOffset + aligned > m_BufferSize) {
        m_CurrentOffset = 0;
    }

    StagingAllocation alloc;
    alloc.offset     = m_CurrentOffset;
    alloc.size       = aligned;
    alloc.cpuPtr     = static_cast<uint8_t*>(m_MappedPtr) + m_CurrentOffset;
    alloc.gpuAddress = 0; // 通过 IRHIBuffer 子类的 GPUAddress 获取
    alloc.valid      = true;

    m_CurrentOffset += aligned;
    return alloc;
}

void StagingBufferManager::CopyToStaging(const StagingAllocation& dst, const void* src, uint64_t size) {
    if (!dst.valid || !dst.cpuPtr || !src || size == 0) return;
    memcpy(dst.cpuPtr, src, size);
}

void StagingBufferManager::EndFrame() {
    m_CurrentOffset = 0;
}

void StagingBufferManager::Shutdown() {
    if (!m_Initialized) return;
    m_StagingBuffer = nullptr;
    m_MappedPtr = nullptr;
    m_CurrentOffset = 0;
    m_BufferSize = 0;
    m_Initialized = false;
}

}} // namespace Engine::RHI