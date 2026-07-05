/**
 * @file TransientHeap.cpp
 * @brief 瞬态显存池实现 — 基于生命周期分析的 First-Fit 分配器
 */

#include "Engine/Rendering/TransientHeap.h"
#include "Engine/Core/Log.h"
#include <algorithm>

namespace {
    Engine::Logger s_Log("TransientHeap");
}

namespace Engine {
namespace Rendering {

    // ════════════════════════════════════════════════════════
    // 构造 / 析构
    // ════════════════════════════════════════════════════════

    TransientHeap::~TransientHeap() {
        Shutdown();
    }

    TransientHeap::TransientHeap(TransientHeap&& other) noexcept
        : m_HeapBuffer(other.m_HeapBuffer)
        , m_MappedPtr(other.m_MappedPtr)
        , m_HeapSize(other.m_HeapSize)
        , m_UsedSize(other.m_UsedSize)
        , m_Initialized(other.m_Initialized)
        , m_Compiled(other.m_Compiled)
        , m_Requests(std::move(other.m_Requests))
        , m_Lifetimes(std::move(other.m_Lifetimes))
        , m_Allocations(std::move(other.m_Allocations))
    {
        other.m_HeapBuffer = nullptr;
        other.m_MappedPtr = nullptr;
        other.m_Initialized = false;
        other.m_Compiled = false;
    }

    TransientHeap& TransientHeap::operator=(TransientHeap&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_HeapBuffer  = other.m_HeapBuffer;
            m_MappedPtr   = other.m_MappedPtr;
            m_HeapSize    = other.m_HeapSize;
            m_UsedSize    = other.m_UsedSize;
            m_Initialized = other.m_Initialized;
            m_Compiled    = other.m_Compiled;
            m_Requests    = std::move(other.m_Requests);
            m_Lifetimes   = std::move(other.m_Lifetimes);
            m_Allocations = std::move(other.m_Allocations);

            other.m_HeapBuffer = nullptr;
            other.m_MappedPtr = nullptr;
            other.m_Initialized = false;
            other.m_Compiled = false;
        }
        return *this;
    }

    // ════════════════════════════════════════════════════════
    // Initialize
    // ════════════════════════════════════════════════════════

    bool TransientHeap::Initialize(RHI::IRHIDevice& device,
                                    uint64_t heapSize) {
        if (m_Initialized) {
            s_Log.Warn("TransientHeap already initialized, shutting down first");
            Shutdown();
        }

        m_HeapSize = heapSize;

        // 创建 GPU Buffer 作为堆的主存储
        RHI::RHIBufferDesc desc;
        desc.size        = m_HeapSize;
        desc.stride      = 0;
        desc.memoryUsage = RHI::MemoryUsage::GPU_Only;  // DEVICE_LOCAL
        desc.initialData = nullptr;

        auto buffer = device.CreateBuffer(desc);
        if (!buffer) {
            s_Log.Error("Failed to create transient heap buffer (size={})", m_HeapSize);
            return false;
        }

        m_HeapBuffer = buffer.get();

        // 尝试映射（仅对 CPU_To_GPU 堆有效）
        auto* allocator = device.GetMemoryAllocator();
        if (allocator) {
            const auto& allocation = m_HeapBuffer->GetAllocation();
            if (allocation.mappedPtr) {
                m_MappedPtr = const_cast<void*>(allocation.mappedPtr);
            }
        }

        m_Initialized = true;
        m_Compiled    = false;

        s_Log.Info("TransientHeap initialized: {} MB", m_HeapSize / (1024 * 1024));
        return true;
    }

    // ════════════════════════════════════════════════════════
    // RegisterRequest
    // ════════════════════════════════════════════════════════

    uint32_t TransientHeap::RegisterRequest(const TransientAllocRequest& request) {
        uint32_t index = static_cast<uint32_t>(m_Requests.size());
        m_Requests.push_back(request);
        return index;
    }

    // ════════════════════════════════════════════════════════
    // Compile — First-Fit 生命周期复用分配
    // ════════════════════════════════════════════════════════

    bool TransientHeap::Compile() {
        if (!m_Initialized) {
            s_Log.Error("TransientHeap not initialized");
            return false;
        }

        m_Lifetimes.clear();
        m_Allocations.clear();

        if (m_Requests.empty()) {
            m_Compiled = true;
            return true;
        }

        // 步骤 1: 构建 LifetimeRange，按 firstUse 排序
        for (uint32_t i = 0; i < m_Requests.size(); ++i) {
            const auto& req = m_Requests[i];
            LifetimeRange lt;
            lt.firstUse      = req.firstUsePass;
            lt.lastUse       = req.lastUsePass;
            lt.size          = AlignUp(req.size, req.alignment);
            lt.offset        = 0;
            lt.resourceIndex = i;
            lt.active        = true;
            m_Lifetimes.push_back(lt);
        }

        // 按 firstUse 排序
        std::sort(m_Lifetimes.begin(), m_Lifetimes.end(),
            [](const LifetimeRange& a, const LifetimeRange& b) {
                return a.firstUse < b.firstUse;
            });

        // 步骤 2: First-Fit 分配
        // 维护一个已分配区间的列表，每个新的请求尝试插入已存在区间的"空洞"中
        struct AllocBlock {
            uint64_t offset;
            uint64_t size;
            uint32_t resourceIndex;
        };
        std::vector<AllocBlock> allocated;

        for (auto& lt : m_Lifetimes) {
            uint64_t bestOffset = m_HeapSize;  // 初始化为无效值
            bool found = false;

            // 尝试将区间放置到已分配区间的空洞中
            uint64_t currentOffset = 0;
            for (const auto& block : allocated) {
                // 检查空洞 [currentOffset, block.offset)
                uint64_t holeStart = currentOffset;
                uint64_t holeEnd   = block.offset;

                // 找到这个空洞对应的已分配资源
                const auto& blockLt = m_Lifetimes[block.resourceIndex];

                // 空洞可用的条件：lt 的生命周期与 blockLt 不重叠
                if (!Overlaps(lt, blockLt)) {
                    // 检查空洞大小是否足够
                    if (holeEnd - holeStart >= lt.size) {
                        bestOffset = holeStart;
                        found = true;
                        break;
                    }
                }

                currentOffset = block.offset + block.size;
            }

            // 检查末尾空洞
            if (!found && m_HeapSize - currentOffset >= lt.size) {
                bestOffset = currentOffset;
                found = true;
            }

            if (!found) {
                s_Log.Error("TransientHeap: out of memory for request {} (size={}, "
                            "firstUse={}, lastUse={})",
                            lt.resourceIndex, lt.size, lt.firstUse, lt.lastUse);
                m_Compiled = false;
                return false;
            }

            lt.offset = bestOffset;

            // 插入分配列表（保持排序）
            AllocBlock newBlock;
            newBlock.offset = bestOffset;
            newBlock.size   = lt.size;
            newBlock.resourceIndex = lt.resourceIndex;
            allocated.push_back(newBlock);

            std::sort(allocated.begin(), allocated.end(),
                [](const AllocBlock& a, const AllocBlock& b) {
                    return a.offset < b.offset;
                });
        }

        // 步骤 3: 构建分配结果
        m_Allocations.resize(m_Requests.size());
        for (const auto& lt : m_Lifetimes) {
            TransientAllocation alloc;
            alloc.offset     = lt.offset;
            alloc.buffer     = m_HeapBuffer;
            alloc.bufferSize = lt.size;
            alloc.valid      = true;

            // 如果有映射指针，计算 CPU 写入地址
            if (m_MappedPtr) {
                alloc.cpuPtr = static_cast<uint8_t*>(m_MappedPtr) + lt.offset;
            }

            m_Allocations[lt.resourceIndex] = alloc;
        }

        // 统计实际使用大小
        uint64_t maxEnd = 0;
        for (const auto& block : allocated) {
            uint64_t end = block.offset + block.size;
            if (end > maxEnd) maxEnd = end;
        }
        m_UsedSize = maxEnd;

        m_Compiled = true;

        s_Log.Info("TransientHeap compiled: {} requests, {}/{} MB used ({:.1f}%)",
                   m_Requests.size(),
                   m_UsedSize / (1024 * 1024),
                   m_HeapSize / (1024 * 1024),
                   GetUtilization() * 100.0f);

        return true;
    }

    // ════════════════════════════════════════════════════════
    // GetAllocation
    // ════════════════════════════════════════════════════════

    TransientAllocation TransientHeap::GetAllocation(uint32_t requestIndex) const {
        if (requestIndex < m_Allocations.size()) {
            return m_Allocations[requestIndex];
        }
        return {};
    }

    // ════════════════════════════════════════════════════════
    // Reset / Shutdown
    // ════════════════════════════════════════════════════════

    void TransientHeap::Reset() {
        m_Requests.clear();
        m_Lifetimes.clear();
        m_Allocations.clear();
        m_Compiled = false;
        s_Log.Info("TransientHeap reset");
    }

    void TransientHeap::Shutdown() {
        if (!m_Initialized) return;

        // Buffer 由 shared_ptr 管理，不需要手动删除
        m_HeapBuffer  = nullptr;
        m_MappedPtr   = nullptr;
        m_HeapSize    = 0;
        m_UsedSize    = 0;
        m_Initialized = false;
        m_Compiled    = false;

        m_Requests.clear();
        m_Lifetimes.clear();
        m_Allocations.clear();

        s_Log.Info("TransientHeap shut down");
    }

} // namespace Rendering
} // namespace Engine