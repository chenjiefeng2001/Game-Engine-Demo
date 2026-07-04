/**
 * @file GPUMemoryAllocatorFallback.cpp
 * @brief 显存分配器软件回退实现 — 无 GPU 依赖，纯软件模拟
 *
 * 匹配 IGPUMemoryAllocator 完整接口：CreateBuffer / CreateImage / Allocate
 * 策略实现：Bump / FrameTransient / Pool / Buddy(委托 Pool)
 */

#include "Engine/Core/RHI/GPUAllocatorFactory.h"
#include "Engine/Core/RHI/GPUMemoryBlock.h"
#include "Engine/Core/RHI/IGPUMemoryAllocator.h"
#include "Engine/Core/Log.h"
#include "Engine/MemoryTracker.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <algorithm>

namespace {
    Engine::Logger s_Log("GPUMemFallback");
}

namespace Engine {
namespace RHI {

    // ============================================================
    // Fallback GPUMemoryBlock — malloc 模拟 GPU 内存块
    // ============================================================
    class FallbackMemoryBlock final : public GPUMemoryBlock {
    public:
        FallbackMemoryBlock(uint64 size, MemoryUsage usage, MemoryProperty props,
                            bool supportsMapping, ResourceNature nature = ResourceNature::Unknown)
            : m_Size(size), m_Usage(usage), m_Properties(props)
            , m_SupportsMapping(supportsMapping), m_Nature(nature)
        {
            m_Memory = static_cast<std::byte*>(std::malloc(static_cast<size_t>(size)));
            if (m_Memory) {
                m_Stats.totalBytes = size;
                m_Stats.properties = props;
                m_Stats.usage = usage;
            }
        }
        ~FallbackMemoryBlock() override {
            if (m_Mapped && m_Memory) Unmap();
            std::free(m_Memory);
            m_Memory = nullptr;
        }

        FallbackMemoryBlock(const FallbackMemoryBlock&) = delete;
        FallbackMemoryBlock& operator=(const FallbackMemoryBlock&) = delete;

        uint64 GetSize() const noexcept override { return m_Size; }
        MemoryProperty GetProperties() const noexcept override { return m_Properties; }
        MemoryUsage GetUsage() const noexcept override { return m_Usage; }
        void* Map(uint64 offset, uint64 size) override {
            if (!m_SupportsMapping || !m_Memory) return nullptr;
            uint64 mapSize = (size == 0) ? (m_Size - offset) : size;
            if (offset + mapSize > m_Size) return nullptr;
            m_MappedOffset = offset; m_MappedSize = mapSize; m_Mapped = true;
            return m_Memory + offset;
        }
        void Unmap() override { m_Mapped = false; m_MappedOffset = 0; m_MappedSize = 0; }
        bool IsMapped() const noexcept override { return m_Mapped; }
        void FlushCPUWrite(uint64, uint64) override {}
        void InvalidateGPUWrite(uint64, uint64) override {}
        uint64 GetDeviceBaseAddr() const noexcept override { return 0; }
        bool SupportsDeviceAddress() const noexcept override { return false; }
        MemoryBlockStats GetStats() const noexcept override { return m_Stats; }
        const char* GetDebugName() const noexcept override { return m_DebugName.c_str(); }

        // ── Fallback 专有 ──
        std::byte* GetRawMemory() noexcept { return m_Memory; }
        ResourceNature GetNature() const noexcept { return m_Nature; }
        void SetDebugName(const char* name) { m_DebugName = name ? name : ""; }
        void UpdateStats(uint64 allocated, uint32 count) {
            m_Stats.allocatedBytes = allocated;
            m_Stats.freeBytes = m_Size > allocated ? (m_Size - allocated) : 0;
            m_Stats.allocationCount = count;
            m_Stats.isFullyFree = (count == 0);
            if (count > m_Stats.maxAllocations) m_Stats.maxAllocations = count;
        }

    private:
        std::byte* m_Memory = nullptr;
        uint64 m_Size = 0;
        MemoryUsage m_Usage = MemoryUsage::GPU_Only;
        MemoryProperty m_Properties = MemoryProperty::DeviceLocal;
        bool m_SupportsMapping = false;
        bool m_Mapped = false;
        uint64 m_MappedOffset = 0;
        uint64 m_MappedSize = 0;
        ResourceNature m_Nature = ResourceNature::Unknown;
        MemoryBlockStats m_Stats{};
        std::string m_DebugName;
    };

    // ============================================================
    // 内部：空闲链表池
    // ============================================================
    struct PoolFreeNode { PoolFreeNode* next = nullptr; };

    struct SubAllocationPool {
        uint64 slotSize = 0;
        PoolFreeNode* freeList = nullptr;
        std::vector<std::unique_ptr<FallbackMemoryBlock>> blocks;
        uint32 allocatedCount = 0;

        void AddBlock(std::unique_ptr<FallbackMemoryBlock> block) {
            uint64 blockSize = block->GetSize();
            uint32 slots = static_cast<uint32>(blockSize / slotSize);
            std::byte* mem = block->GetRawMemory();
            for (uint32 i = 0; i < slots; ++i) {
                auto* node = reinterpret_cast<PoolFreeNode*>(mem + i * slotSize);
                node->next = freeList;
                freeList = node;
            }
            blocks.push_back(std::move(block));
        }
    };

    // ============================================================
    // FallbackGPUAllocator
    // ============================================================
    class FallbackGPUAllocator final : public IGPUMemoryAllocator {
    public:
        FallbackGPUAllocator() = default;
        ~FallbackGPUAllocator() override { Shutdown(); }

        // ── 初始化 ──
        bool Initialize(const GPUMemoryConfig& config) override {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_Initialized) Shutdown();
            m_Config = config;
            m_Initialized = true;
            s_Log.Info("Fallback GPU Allocator initialized (device={}MB, host={}MB, FIF={})",
                (size_t)(config.deviceLocalBlockSize / (1024*1024)),
                (size_t)(config.hostVisibleBlockSize / (1024*1024)),
                config.framesInFlight);
            for (const auto& preset : config.poolPresets)
                CreatePoolInternal(preset.type, preset.slotSize, preset.initialSlots);
            return true;
        }

        void Shutdown() override {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Blocks.clear(); m_Pools.clear(); m_BumpBlocks.clear();
            m_TransientBlocks.clear(); m_Stats = MemoryStats{};
            m_Initialized = false;
        }

        bool IsReady() const noexcept override { return m_Initialized; }

        // ══════════════════════════════════════════════════════
        // 模式 A：CreateBuffer / CreateImage
        // ══════════════════════════════════════════════════════

        GPUAllocation CreateBuffer(const BufferDesc& desc, MemoryUsage usage,
                                   AllocationStrategy strategy,
                                   GPUResourceType resourceType,
                                   const char* debugTag) override
        {
            // Fallback: apiResource 为分配给 Buffer 的内存指针（模拟 GL Buffer ID）
            // OpenGL 后端可重写以返回真正的 GLuint
            auto alloc = Allocate(desc.size, 0, usage, 0, strategy,
                                  ResourceNature::Linear, resourceType, debugTag);

            if (alloc.IsValid()) {
                // Fallback 下 apiResource 复用 mappedPtr（或首地址）
                alloc.apiResource = alloc.mappedPtr ? alloc.mappedPtr
                                      : static_cast<FallbackMemoryBlock*>(alloc.block)->GetRawMemory() + alloc.offset;
                // Fallback 无独立内存句柄
                alloc.apiMemoryHandle = nullptr;

                if (desc.initialData && alloc.mappedPtr) {
                    std::memcpy(alloc.mappedPtr, desc.initialData,
                        static_cast<size_t>(std::min(desc.size, alloc.size)));
                }
            }
            return alloc;
        }

        GPUAllocation CreateImage(const ImageDesc& desc, MemoryUsage usage,
                                  AllocationStrategy strategy,
                                  GPUResourceType resourceType,
                                  const char* debugTag) override
        {
            // Fallback: 简化估算（OpenGL 真实后端需要 glTexStorage2D）
            uint64 estimatedSize = CalculateImageSize(desc);
            auto alloc = Allocate(estimatedSize, 0, usage, 0, strategy,
                                  ResourceNature::Image, resourceType, debugTag);

            if (alloc.IsValid()) {
                alloc.apiResource = alloc.mappedPtr ? alloc.mappedPtr
                    : static_cast<FallbackMemoryBlock*>(alloc.block)->GetRawMemory() + alloc.offset;
                alloc.apiMemoryHandle = nullptr;
            }
            return alloc;
        }

        // ══════════════════════════════════════════════════════
        // 模式 B：纯内存分配
        // ══════════════════════════════════════════════════════

        GPUAllocation Allocate(uint64 size, uint64 alignment,
                               MemoryUsage usage, uint32 memoryTypeBits,
                               AllocationStrategy strategy,
                               ResourceNature nature,
                               GPUResourceType resourceType,
                               const char* debugTag) override
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            (void)memoryTypeBits; // Fallback 忽略内存类型掩码

            if (!m_Initialized || size == 0) return NullAllocation();
            if (alignment == 0) alignment = 256;
            uint64 alignedSize = (size + alignment - 1) & ~(alignment - 1);

            // 从 GPUResourceType 推断 nature（若未指定）
            if (nature == ResourceNature::Unknown)
                nature = GPUResourceTypeToNature(resourceType);

            GPUAllocation result;

            switch (strategy) {
                case AllocationStrategy::Bump:
                    result = AllocateBump(alignedSize, alignment, usage, nature);
                    break;
                case AllocationStrategy::FrameTransient:
                    result = AllocateFrameTransient(alignedSize, alignment, usage, nature);
                    break;
                case AllocationStrategy::Pool:
                    result = AllocatePool(alignedSize, alignment, usage, nature, resourceType);
                    break;
                case AllocationStrategy::Buddy:
                default:
                    result = AllocatePool(alignedSize, alignment, usage, nature, resourceType);
                    break;
            }

            if (result.IsValid()) {
                result.requestedSize = size;
                result.debugTag = debugTag;
                result.resourceType = resourceType;
                result.usage = usage;
                result.strategy = strategy;
                result.nature = nature;

                m_Stats.allocationCount++;
                m_Stats.totalSubAllocations++;
                m_Stats.totalUsed += result.size;
                if (m_Stats.allocationCount > m_Stats.peakAllocationCount)
                    m_Stats.peakAllocationCount = m_Stats.allocationCount;

                if (nature == ResourceNature::Linear) {
                    m_Stats.linearAllocations++;
                    m_Stats.linearBytes += result.size;
                } else if (nature == ResourceNature::Image) {
                    m_Stats.imageAllocations++;
                    m_Stats.imageBytes += result.size;
                }

                MemoryTracker::OnAlloc(MemCategory::GPU, static_cast<size_t>(result.size),
                    debugTag ? debugTag : "GPU_Fallback");
            } else {
                m_Stats.failedAllocations++;
                if (m_OOMCallback) m_OOMCallback(size, usage, debugTag, m_OOMUserData);
            }
            return result;
        }

        void Deallocate(GPUAllocation& allocation) override {
            if (!allocation.IsValid()) return;
            std::lock_guard<std::mutex> lock(m_Mutex);

            MemoryTracker::OnFree(MemCategory::GPU, static_cast<size_t>(allocation.size));
            m_Stats.totalUsed -= allocation.size;
            if (m_Stats.allocationCount > 0) m_Stats.allocationCount--;

            // Fallback: 无实际 API 资源可销毁（apiResource 是 malloc 指针，由 Block 统一管理）
            // 真实的 Vulkan/D3D12 后端应在此处调用 vkDestroyBuffer / ID3D12Resource::Release

            switch (allocation.strategy) {
                case AllocationStrategy::Pool:
                case AllocationStrategy::Buddy:
                    if (allocation.mappedPtr)
                        std::memset(allocation.mappedPtr, 0, static_cast<size_t>(allocation.size));
                    break;
                case AllocationStrategy::Bump:
                case AllocationStrategy::FrameTransient:
                    // 不支持单独释放
                    break;
            }
            allocation = GPUAllocation{};
        }

        using IGPUMemoryAllocator::AllocateStatic;
        using IGPUMemoryAllocator::AllocateDynamic;
        using IGPUMemoryAllocator::AllocateStaging;
        using IGPUMemoryAllocator::AllocateReadback;

        // ── 一致性 ──
        void FlushAllocation(const GPUAllocation&, uint64, uint64) override {}
        void InvalidateAllocation(const GPUAllocation&, uint64, uint64) override {}

        // ── 内存块管理 ──
        void Defragment() override {
            s_Log.Info("Fallback Defragment: not implemented (no-op)");
        }

        void Trim() override {
            std::lock_guard<std::mutex> lock(m_Mutex);
            TrimInternal();
        }

        void EndFrame() override {
            // FrameTransient: 推进当前帧索引（环形缓冲切换）
            m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % m_Config.framesInFlight;

            // 周期性 Trim
            m_FrameCounter++;
            if (m_Config.trimFrameInterval > 0 &&
                (m_FrameCounter % m_Config.trimFrameInterval) == 0) {
                Trim();
            }
        }

        // ── 统计 ──
        MemoryStats GetStats() const override {
            std::lock_guard<std::mutex> lock(m_Mutex);
            MemoryStats s = m_Stats;
            s.totalAllocated = 0;
            for (const auto& block : m_Blocks) s.totalAllocated += block->GetSize();
            s.totalFree = s.totalAllocated > s.totalUsed ? (s.totalAllocated - s.totalUsed) : 0;
            s.blockCount = static_cast<uint32>(m_Blocks.size());
            s.fullyFreeBlockCount = 0;
            for (const auto& block : m_Blocks)
                if (block->GetStats().isFullyFree) s.fullyFreeBlockCount++;
            if (s.totalAllocated > 0)
                s.internalFragmentation = static_cast<float>(s.totalAllocated - s.totalUsed)
                                          / static_cast<float>(s.totalAllocated);
            return s;
        }

        std::string DumpStats() const override {
            auto s = GetStats();
            char buf[600];
            snprintf(buf, sizeof(buf),
                "FallbackGPUAllocator:\n"
                "  Total: %zu MB (peak: %zu MB)\n"
                "  Used:  %zu MB (peak: %zu MB), Free: %zu MB\n"
                "  Blocks: %u (peak: %u, free: %u)\n"
                "  Allocs: %zu (peak: %zu, failed: %zu)\n"
                "  Linear: %zu allocs (%zu MB), Image: %zu allocs (%zu MB)\n"
                "  Fragmentation: %.1f%%",
                (size_t)(s.totalAllocated/(1024*1024)), (size_t)(s.peakAllocated/(1024*1024)),
                (size_t)(s.totalUsed/(1024*1024)), (size_t)(s.peakUsed/(1024*1024)),
                (size_t)(s.totalFree/(1024*1024)),
                s.blockCount, s.peakBlockCount, s.fullyFreeBlockCount,
                (size_t)s.allocationCount, (size_t)s.peakAllocationCount,
                (size_t)s.failedAllocations,
                (size_t)s.linearAllocations, (size_t)(s.linearBytes/(1024*1024)),
                (size_t)s.imageAllocations, (size_t)(s.imageBytes/(1024*1024)),
                s.internalFragmentation*100.0f);
            return buf;
        }

        void LogStats() const override { s_Log.Info("{}", DumpStats()); }

        void SetOOMCallback(OOMCallback callback, void* userData) override {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_OOMCallback = callback; m_OOMUserData = userData;
        }

        std::vector<MemoryBlockStats> GetPerBlockStats() const override {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<MemoryBlockStats> result;
            result.reserve(m_Blocks.size());
            for (const auto& block : m_Blocks) result.push_back(block->GetStats());
            return result;
        }

        void UpdateConfig(const GPUMemoryConfig& config) override {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Config = config;
        }

        const GPUMemoryConfig& GetConfig() const noexcept override { return m_Config; }

    private:
        // ══════════════════════════════════════════════════════
        // 策略：Pool
        // ══════════════════════════════════════════════════════

        GPUAllocation AllocatePool(uint64 alignedSize, uint64 /*alignment*/,
                                   MemoryUsage usage, ResourceNature nature,
                                   GPUResourceType resourceType)
        {
            auto it = m_Pools.find(resourceType);
            if (it != m_Pools.end()) {
                auto& pool = it->second;
                if (pool.slotSize >= alignedSize && pool.freeList) {
                    auto* node = pool.freeList;
                    pool.freeList = node->next;
                    pool.allocatedCount++;

                    GPUAllocation alloc;
                    alloc.size = pool.slotSize; alloc.usage = usage;
                    alloc.properties = MemoryUsageToProperty(usage);
                    alloc.strategy = AllocationStrategy::Pool;
                    alloc.nature = nature;
                    alloc.mappedPtr = node; alloc.mappedSize = pool.slotSize;
                    return alloc;
                }
            }

            auto block = CreateBlockInternal(usage, ResourceNature::Linear,
                std::max(alignedSize * 16, m_Config.deviceLocalBlockSize));
            if (!block) return NullAllocation();

            m_Blocks.push_back(std::move(block));
            auto& newBlock = static_cast<FallbackMemoryBlock&>(*m_Blocks.back());
            newBlock.SetDebugName("FallbackPool");

            GPUAllocation alloc;
            alloc.block = &newBlock; alloc.offset = 0;
            alloc.size = alignedSize; alloc.usage = usage;
            alloc.properties = MemoryUsageToProperty(usage);
            alloc.strategy = AllocationStrategy::Pool;
            alloc.nature = nature;
            alloc.mappedPtr = newBlock.GetRawMemory();
            alloc.mappedSize = alignedSize;
            newBlock.UpdateStats(alignedSize, 1);
            return alloc;
        }

        // ══════════════════════════════════════════════════════
        // 策略：Bump
        // ══════════════════════════════════════════════════════

        GPUAllocation AllocateBump(uint64 alignedSize, uint64 alignment,
                                   MemoryUsage usage, ResourceNature nature)
        {
            FallbackMemoryBlock* targetBlock = nullptr;
            uint64 bumpOffset = 0;

            auto key = static_cast<uint8>(usage);
            auto& bumpList = m_BumpBlocks[key];

            for (auto& entry : bumpList) {
                auto& block = entry.first;
                uint64& offset = entry.second;
                uint64 aligned = (offset + alignment - 1) & ~(alignment - 1);
                if (aligned + alignedSize <= block->GetSize()) {
                    targetBlock = static_cast<FallbackMemoryBlock*>(block.get());
                    bumpOffset = aligned;
                    offset = aligned + alignedSize;
                    targetBlock->UpdateStats(offset, targetBlock->GetStats().allocationCount + 1);
                    break;
                }
            }

            if (!targetBlock) {
                uint64 blockSize = std::max(alignedSize * 8,
                    (usage == MemoryUsage::GPU_Only) ? m_Config.deviceLocalBlockSize
                                                     : m_Config.hostVisibleBlockSize);
                auto block = CreateBlockInternal(usage, nature, blockSize);
                if (!block) return NullAllocation();
                block->SetDebugName("FallbackBump");
                uint64& offset = bumpList.emplace_back(std::move(block), alignedSize).second;

                auto& entry = bumpList.back();
                targetBlock = static_cast<FallbackMemoryBlock*>(entry.first.get());
                targetBlock->UpdateStats(alignedSize, 1);
                bumpOffset = 0;
            }

            GPUAllocation alloc;
            alloc.block = targetBlock; alloc.offset = bumpOffset;
            alloc.size = alignedSize; alloc.usage = usage;
            alloc.properties = MemoryUsageToProperty(usage);
            alloc.strategy = AllocationStrategy::Bump;
            alloc.nature = nature;

            if (HasProperty(alloc.properties, MemoryProperty::HostVisible)) {
                alloc.mappedPtr = targetBlock->GetRawMemory() + bumpOffset;
                alloc.mappedSize = alignedSize;
            }
            return alloc;
        }

        // ══════════════════════════════════════════════════════
        // 策略：FrameTransient（环形缓冲）
        // ══════════════════════════════════════════════════════

        GPUAllocation AllocateFrameTransient(uint64 alignedSize, uint64 alignment,
                                             MemoryUsage usage, ResourceNature nature)
        {
            // 按 (usage, nature, frameIndex) 分桶
            uint64 key = (static_cast<uint64>(usage) << 16)
                       | (static_cast<uint64>(static_cast<uint8>(nature)) << 8)
                       | m_CurrentFrameIndex;

            auto& transientList = m_TransientBlocks[key];

            FallbackMemoryBlock* targetBlock = nullptr;
            uint64 offset = 0;

            for (auto& entry : transientList) {
                auto& block = entry.first;
                uint64& cursor = entry.second;
                uint64 aligned = (cursor + alignment - 1) & ~(alignment - 1);
                if (aligned + alignedSize <= block->GetSize()) {
                    targetBlock = static_cast<FallbackMemoryBlock*>(block.get());
                    offset = aligned;
                    cursor = aligned + alignedSize;
                    targetBlock->UpdateStats(cursor, targetBlock->GetStats().allocationCount + 1);
                    break;
                }
            }

            if (!targetBlock) {
                uint64 blockSize = std::max(alignedSize * 16, m_Config.hostVisibleBlockSize);
                if (m_Config.transientBudgetPerFrame > 0 && blockSize > m_Config.transientBudgetPerFrame)
                    blockSize = m_Config.transientBudgetPerFrame;

                auto block = CreateBlockInternal(usage, nature, blockSize);
                if (!block) return NullAllocation();
                block->SetDebugName("FallbackFrameTransient");
                auto rawPtr = block.get();
                uint64& cursor = transientList.emplace_back(std::move(block), alignedSize).second;
                targetBlock = rawPtr;
                rawPtr->UpdateStats(alignedSize, 1);
                offset = 0;
            }

            GPUAllocation alloc;
            alloc.block = targetBlock; alloc.offset = offset;
            alloc.size = alignedSize; alloc.usage = usage;
            alloc.properties = MemoryUsageToProperty(usage);
            alloc.strategy = AllocationStrategy::FrameTransient;
            alloc.nature = nature;

            if (HasProperty(alloc.properties, MemoryProperty::HostVisible)) {
                alloc.mappedPtr = targetBlock->GetRawMemory() + offset;
                alloc.mappedSize = alignedSize;
            }
            return alloc;
        }

        void TrimInternal() {
            auto it = m_Blocks.begin();
            while (it != m_Blocks.end()) {
                auto* block = static_cast<FallbackMemoryBlock*>(it->get());
                if (block->GetStats().isFullyFree) {
                    for (auto& [key, bumpList] : m_BumpBlocks)
                        bumpList.erase(std::remove_if(bumpList.begin(), bumpList.end(),
                            [&](const auto& p) { return p.first.get() == block; }), bumpList.end());
                    for (auto& [key, transList] : m_TransientBlocks)
                        transList.erase(std::remove_if(transList.begin(), transList.end(),
                            [&](const auto& p) { return p.first.get() == block; }), transList.end());
                    it = m_Blocks.erase(it);
                } else { ++it; }
            }
        }

        std::unique_ptr<FallbackMemoryBlock> CreateBlockInternal(
            MemoryUsage usage, ResourceNature nature, uint64 size)
        {
            auto props = MemoryUsageToProperty(usage);
            bool canMap = HasProperty(props, MemoryProperty::HostVisible);
            return std::make_unique<FallbackMemoryBlock>(size, usage, props, canMap, nature);
        }

        void CreatePoolInternal(GPUResourceType type, uint64 slotSize, uint32 initialSlots) {
            auto& pool = m_Pools[type];
            pool.slotSize = slotSize;
            if (initialSlots > 0) {
                uint64 blockSize = slotSize * initialSlots;
                auto block = std::make_unique<FallbackMemoryBlock>(
                    blockSize, MemoryUsage::GPU_Only, MemoryProperty::DeviceLocal, false,
                    GPUResourceTypeToNature(type));
                block->SetDebugName(GPUResourceTypeName(type));
                pool.AddBlock(std::move(block));
                if (!pool.blocks.empty()) {
                    FallbackMemoryBlock* rawPtr = pool.blocks.back().get();
                    m_Blocks.push_back(GPUMemoryBlockPtr(static_cast<GPUMemoryBlock*>(rawPtr)));
                }
            }
        }

        uint64 CalculateImageSize(const ImageDesc& desc) const {
            // 简化估算：每像素 4 字节 RGBA * mip 链
            uint32 bpp = 4;
            uint64 total = 0;
            uint32 w = desc.width, h = desc.height, d = desc.depth;
            for (uint32 mip = 0; mip < desc.mipLevels; ++mip) {
                total += static_cast<uint64>(w) * h * d * bpp;
                w = std::max(1u, w / 2);
                h = std::max(1u, h / 2);
                d = std::max(1u, d / 2);
            }
            return total * desc.arrayLayers * desc.sampleCount;
        }

        // ── 数据成员 ──
        mutable std::mutex m_Mutex;
        bool m_Initialized = false;
        GPUMemoryConfig m_Config;

        std::vector<GPUMemoryBlockPtr> m_Blocks;
        std::unordered_map<GPUResourceType, SubAllocationPool> m_Pools;

        // Bump: (usage → (block, offset))
        mutable std::unordered_map<uint8, std::vector<
            std::pair<GPUMemoryBlockPtr, uint64>>> m_BumpBlocks;

        // FrameTransient: (usage|nature|frameIndex → (block, offset))
        mutable std::unordered_map<uint64, std::vector<
            std::pair<GPUMemoryBlockPtr, uint64>>> m_TransientBlocks;

        MemoryStats m_Stats;
        OOMCallback m_OOMCallback = nullptr;
        void* m_OOMUserData = nullptr;
        uint64 m_FrameCounter = 0;
        uint32 m_CurrentFrameIndex = 0;
    };

    // ============================================================
    // 工厂函数
    // ============================================================
    GPUMemoryAllocatorPtr CreateGPUMemoryAllocator(
        AllocatorBackend backend, const GPUMemoryConfig& config)
    {
        AllocatorBackend actualBackend = backend;
        if (backend == AllocatorBackend::Default)
            actualBackend = GetDefaultBackend();

        s_Log.Info("Creating GPU allocator: backend={}", AllocatorBackendName(actualBackend));

        GPUMemoryAllocatorPtr allocator;
        switch (actualBackend) {
            case AllocatorBackend::VMA:
                s_Log.Warn("VMA not compiled, falling back to Software.");
                allocator = std::make_unique<FallbackGPUAllocator>();
                break;
            case AllocatorBackend::D3D12MA:
                s_Log.Warn("D3D12MA not compiled, falling back to Software.");
                allocator = std::make_unique<FallbackGPUAllocator>();
                break;
            case AllocatorBackend::Fallback:
            case AllocatorBackend::Default:
            default:
                allocator = std::make_unique<FallbackGPUAllocator>();
                break;
        }

        if (!allocator) {
            s_Log.Error("Failed to create GPU allocator instance");
            return nullptr;
        }

        if (!allocator->Initialize(config)) {
            s_Log.Error("Failed to initialize GPU allocator");
            return nullptr;
        }
        return allocator;
    }

    bool IsBackendAvailable(AllocatorBackend backend) noexcept {
        switch (backend) {
            case AllocatorBackend::Fallback:
            case AllocatorBackend::Default: return true;
            default: return false;
        }
    }

    AllocatorBackend GetDefaultBackend() noexcept {
        return AllocatorBackend::Fallback;
    }

} // namespace RHI
} // namespace Engine