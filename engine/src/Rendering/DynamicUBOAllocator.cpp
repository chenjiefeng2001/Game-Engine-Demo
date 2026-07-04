/**
 * @file DynamicUBOAllocator.cpp
 * @brief 动态 Uniform Buffer 分配器实现
 */

#include "Engine/Rendering/DynamicUBOAllocator.h"
#include "Engine/Core/Log.h"
#include <cstring>

namespace {
    Engine::Logger s_Log("DynamicUBO");
}

namespace Engine {
namespace Rendering {

    // ════════════════════════════════════════════════════════
    // 构造 / 析构
    // ════════════════════════════════════════════════════════

    DynamicUBOAllocator::~DynamicUBOAllocator() {
        Shutdown();
    }

    DynamicUBOAllocator::DynamicUBOAllocator(DynamicUBOAllocator&& other) noexcept
        : m_Buffer(other.m_Buffer)
        , m_MappedPtr(other.m_MappedPtr)
        , m_BufferSize(other.m_BufferSize)
        , m_Alignment(other.m_Alignment)
        , m_CurrentOffset(other.m_CurrentOffset)
        , m_Initialized(other.m_Initialized)
        , m_CurrentFrame(other.m_CurrentFrame)
    {
        other.m_Buffer = nullptr;
        other.m_MappedPtr = nullptr;
        other.m_Initialized = false;
    }

    DynamicUBOAllocator& DynamicUBOAllocator::operator=(DynamicUBOAllocator&& other) noexcept {
        if (this != &other) {
            Shutdown();

            m_Buffer       = other.m_Buffer;
            m_MappedPtr    = other.m_MappedPtr;
            m_BufferSize   = other.m_BufferSize;
            m_Alignment    = other.m_Alignment;
            m_CurrentOffset = other.m_CurrentOffset;
            m_Initialized  = other.m_Initialized;
            m_CurrentFrame = other.m_CurrentFrame;

            other.m_Buffer = nullptr;
            other.m_MappedPtr = nullptr;
            other.m_Initialized = false;
        }
        return *this;
    }

    // ════════════════════════════════════════════════════════
    // Initialize
    // ════════════════════════════════════════════════════════

    bool DynamicUBOAllocator::Initialize(RHI::IRHIDevice& device,
                                          uint64_t bufferSize,
                                          uint64_t alignment)
    {
        if (m_Initialized) {
            s_Log.Warn("DynamicUBOAllocator already initialized, shutting down first");
            Shutdown();
        }

        // 对齐到 256 字节（Vulkan minUniformBufferOffsetAlignment 的安全值）
        m_Alignment = (alignment < 256) ? 256 : alignment;
        m_BufferSize = bufferSize;

        // 创建 CPU-visible GPU Buffer
        RHI::RHIBufferDesc desc;
        desc.size        = m_BufferSize;
        desc.stride      = 0;
        desc.memoryUsage = RHI::MemoryUsage::CPU_To_GPU;  // HOST_VISIBLE | HOST_COHERENT
        desc.initialData = nullptr;

        auto buffer = device.CreateBuffer(desc);
        if (!buffer) {
            s_Log.Error("Failed to create DynamicUBO buffer (size={})", m_BufferSize);
            return false;
        }

        m_Buffer = buffer.get();

        // 持久映射（对于 HOST_VISIBLE | HOST_COHERENT 内存是合法的）
        // 在 Vulkan 中: vkMapMemory(device, allocation.memory, allocation.offset, m_BufferSize, 0, &m_MappedPtr)
        // 在 D3D12 中: ID3D12Resource::Map(0, nullptr, &m_MappedPtr)
        //
        // 由于 RHI 抽象未提供 Map 接口，此处通过 IGPUMemoryAllocator 获取映射指针
        auto* allocator = device.GetMemoryAllocator();
        if (allocator) {
            const auto& allocation = m_Buffer->GetAllocation();
            if (allocation.mappedPtr) {
                m_MappedPtr = allocation.mappedPtr;
            } else {
                // 尝试映射
                // 在完整 RHI 实现中应通过: allocator->Map(allocation.memory, &m_MappedPtr);
                // 目前通过 Buffer 的 allocation 直接获取
                m_MappedPtr = const_cast<void*>(allocation.mappedPtr);
            }
        }

        if (!m_MappedPtr) {
            s_Log.Warn("DynamicUBO buffer not mapped, will use staging upload fallback");
            // 降级方案：如果无法映射，则通过 cmd.UpdateSubresource 上传
            // 这在 OpenGL 后端是正常情况
        }

        m_CurrentOffset = 0;
        m_CurrentFrame  = 0;
        m_Initialized   = true;

        s_Log.Info("DynamicUBOAllocator initialized: size={}MB, alignment={}",
                   m_BufferSize / (1024 * 1024), m_Alignment);
        return true;
    }

    // ════════════════════════════════════════════════════════
    // Allocate
    // ════════════════════════════════════════════════════════

    UBOAllocation DynamicUBOAllocator::Allocate(uint64_t size) {
        if (!m_Initialized) {
            s_Log.Error("DynamicUBOAllocator not initialized");
            return {};
        }

        // 对齐
        uint64_t alignedSize = (size + m_Alignment - 1) & ~(m_Alignment - 1);
        if (alignedSize == 0) alignedSize = m_Alignment;

        // 检查是否溢出
        if (m_CurrentOffset + alignedSize > m_BufferSize) {
            s_Log.Error("DynamicUBO out of memory: requested {} (aligned {}), "
                        "allocated {}/{}", size, alignedSize,
                        m_CurrentOffset, m_BufferSize);
            return {};
        }

        UBOAllocation alloc;
        alloc.gpuOffset = m_CurrentOffset;
        alloc.size      = alignedSize;
        alloc.valid     = true;

        // 如果有映射指针，直接返回 CPU 写入地址
        if (m_MappedPtr) {
            alloc.cpuPtr = static_cast<uint8_t*>(m_MappedPtr) + m_CurrentOffset;
        }

        m_CurrentOffset += alignedSize;

        return alloc;
    }

    // ════════════════════════════════════════════════════════
    // EndFrame
    // ════════════════════════════════════════════════════════

    void DynamicUBOAllocator::EndFrame(uint64_t frameIndex) {
        if (!m_Initialized) return;

        m_CurrentFrame = frameIndex;

        // 重置分配指针 — 假设 GPU 已经消费完上一帧的数据
        // 在双/三缓冲场景中，应追踪 GPU fence 确保安全重置
        m_CurrentOffset = 0;

        // 清空映射缓存（实际数据仍然存在，但旧指针不再有效）
        // 不解除映射

        s_Log.Info("DynamicUBO: Frame {} end, reset allocator (utilization {:.1f}%)",
                   frameIndex, (float)m_CurrentOffset / (float)m_BufferSize * 100.0f);
    }

    // ════════════════════════════════════════════════════════
    // Shutdown
    // ════════════════════════════════════════════════════════

    void DynamicUBOAllocator::Shutdown() {
        if (!m_Initialized) return;

        // Buffer 由 shared_ptr 管理，不需要手动删除
        // 在 IRHIDevice 析构时自动释放
        m_Buffer     = nullptr;
        m_MappedPtr  = nullptr;
        m_Initialized = false;

        s_Log.Info("DynamicUBOAllocator shut down");
    }

} // namespace Rendering
} // namespace Engine