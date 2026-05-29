#include "Engine/Core/Resources/ResourcePoolAllocator.h"
#include "Engine/Core/Log.h"
#include <cstdlib>
#include <cstring>

namespace {
    Engine::Logger s_Log("PoolAllocator");
}

namespace Engine {

    // ============================================================
    // 析构
    // ============================================================

    ResourcePoolAllocator::~ResourcePoolAllocator() {
        Clear();
    }

    // ============================================================
    // 池管理
    // ============================================================

    void ResourcePoolAllocator::CreatePool(ResourceType type, size_t slotSize,
                                           size_t initialSlots, size_t alignment) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        // 对齐 slotSize
        size_t alignedSlot = slotSize;
        if (alignment > alignof(std::max_align_t)) {
            // 需要超对齐
            alignedSlot = (slotSize + alignment - 1) & ~(alignment - 1);
        } else {
            alignedSlot = (slotSize + alignof(std::max_align_t) - 1) &
                          ~(alignof(std::max_align_t) - 1);
        }

        Pool pool;
        pool.config.slotSize  = alignedSlot;
        pool.config.alignment = alignment;
        pool.config.blockSize = alignedSlot * initialSlots;
        if (pool.config.blockSize < 4096)
            pool.config.blockSize = 4096;  // 最小块 4KB

        m_Pools[type] = std::move(pool);

        // 预分配初始块
        AllocateNewBlock(m_Pools[type]);
    }

    void ResourcePoolAllocator::DestroyPool(ResourceType type) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pools.erase(type);
    }

    bool ResourcePoolAllocator::HasPool(ResourceType type) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Pools.count(type) > 0;
    }

    // ============================================================
    // 分配新块
    // ============================================================

    void ResourcePoolAllocator::AllocateNewBlock(Pool& pool) {
        size_t blockBytes = pool.config.blockSize;
        size_t slotSize   = pool.config.slotSize;
        size_t alignment  = pool.config.alignment;

        // 计算每个块的槽数
        size_t slotsPerBlock = blockBytes / slotSize;
        if (slotsPerBlock == 0) slotsPerBlock = 1;

        // 分配对齐内存
        size_t allocSize = slotsPerBlock * slotSize;
        char* memory = nullptr;

        if (alignment > alignof(std::max_align_t)) {
            // 超对齐分配
#ifdef _WIN32
            memory = static_cast<char*>(_aligned_malloc(allocSize, alignment));
#else
            if (::posix_memalign(reinterpret_cast<void**>(&memory), alignment, allocSize) != 0)
                memory = nullptr;
#endif
        } else {
            memory = new char[allocSize];
        }

        if (!memory) {
            s_Log.Error("Failed to allocate block of {} bytes", allocSize);
            return;
        }

        Block block;
        block.memory = memory;
        block.size   = allocSize;

        // 将新块中的所有槽加入空闲链表
        for (size_t i = 0; i < slotsPerBlock; ++i) {
            char* slot = memory + i * slotSize;
            auto* node = reinterpret_cast<FreeNode*>(slot);
            node->next = pool.freeList;
            pool.freeList = node;
        }

        pool.blocks.push_back(std::move(block));
    }

    // ============================================================
    // 分配
    // ============================================================

    void* ResourcePoolAllocator::Allocate(ResourceType type) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = m_Pools.find(type);
        if (it == m_Pools.end()) {
            s_Log.Error("No pool for type {}", static_cast<int>(type));
            return nullptr;
        }

        Pool& pool = it->second;

        // 空闲链表为空时分配新块
        if (!pool.freeList) {
            AllocateNewBlock(pool);
            if (!pool.freeList) {
                s_Log.Error("Out of memory for type {}", static_cast<int>(type));
                return nullptr;
            }
        }

        // 从空闲链表取头节点
        auto* node = pool.freeList;
        pool.freeList = node->next;

        pool.allocatedCount++;
        if (pool.allocatedCount > pool.peakAllocated)
            pool.peakAllocated = pool.allocatedCount;

        // 清零分配的内存
        std::memset(node, 0, pool.config.slotSize);

        return static_cast<void*>(node);
    }

    // ============================================================
    // 释放
    // ============================================================

    void ResourcePoolAllocator::Deallocate(ResourceType type, void* ptr) {
        if (!ptr) return;

        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = m_Pools.find(type);
        if (it == m_Pools.end()) return;

        Pool& pool = it->second;
        auto* node = static_cast<FreeNode*>(ptr);

        // 归还到空闲链表
        node->next = pool.freeList;
        pool.freeList = node;

        if (pool.allocatedCount > 0)
            pool.allocatedCount--;
    }

    // ============================================================
    // 批量操作
    // ============================================================

    void ResourcePoolAllocator::Trim() {
        std::lock_guard<std::mutex> lock(m_Mutex);

        for (auto& [type, pool] : m_Pools) {
            // 如果所有槽都空闲，释放多余的块（保留至少一个）
            if (pool.allocatedCount == 0 && pool.blocks.size() > 1) {
                // 释放除第一个块外的所有块
                for (size_t i = 1; i < pool.blocks.size(); ++i) {
                    // 从空闲链表中移除该块的槽
                    // 注意：简单实现，假设所有槽都在空闲链表中
                }
                pool.blocks.resize(1);
                // 重新构建空闲链表
                pool.freeList = nullptr;
                Block& block = pool.blocks[0];
                size_t slotSize = pool.config.slotSize;
                size_t slotsPerBlock = block.size / slotSize;
                for (size_t i = 0; i < slotsPerBlock; ++i) {
                    char* slot = block.memory + i * slotSize;
                    auto* node = reinterpret_cast<FreeNode*>(slot);
                    node->next = pool.freeList;
                    pool.freeList = node;
                }
            }
        }
    }

    void ResourcePoolAllocator::Clear() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pools.clear();
    }

    // ============================================================
    // 统计
    // ============================================================

    PoolStats ResourcePoolAllocator::GetPoolStats(ResourceType type) const {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = m_Pools.find(type);
        if (it == m_Pools.end()) return {};

        const Pool& pool = it->second;
        PoolStats stats;
        stats.slotSize       = pool.config.slotSize;
        stats.blockCount     = pool.blocks.size();

        size_t slotsPerBlock = 0;
        if (!pool.blocks.empty())
            slotsPerBlock = pool.blocks[0].size / pool.config.slotSize;
        stats.slotsPerBlock  = slotsPerBlock;
        stats.allocatedSlots = pool.allocatedCount;

        // 计算空闲槽数
        size_t totalSlots = stats.blockCount * stats.slotsPerBlock;
        stats.freeSlots = totalSlots - stats.allocatedSlots;

        for (const auto& block : pool.blocks)
            stats.totalBytes += block.size;
        stats.usedBytes = stats.allocatedSlots * stats.slotSize;
        stats.peakAllocated = pool.peakAllocated;

        return stats;
    }

    ResourcePoolAllocator::GlobalStats ResourcePoolAllocator::GetGlobalStats() const {
        std::lock_guard<std::mutex> lock(m_Mutex);

        GlobalStats gs;
        gs.poolCount = m_Pools.size();
        for (const auto& [type, pool] : m_Pools) {
            size_t blockBytes = 0;
            for (const auto& block : pool.blocks)
                blockBytes += block.size;
            gs.totalBytes += blockBytes;
            gs.totalAllocated += pool.allocatedCount;
            size_t totalSlots = 0;
            if (!pool.blocks.empty())
                totalSlots = pool.blocks.size() * (pool.blocks[0].size / pool.config.slotSize);
            gs.totalFree += totalSlots - pool.allocatedCount;
        }
        return gs;
    }

    void ResourcePoolAllocator::LogStats() const {
        auto gs = GetGlobalStats();
        s_Log.Info("=== PoolAllocator Stats ===");
        s_Log.Info("Pools: {}, Total: {}KB, Used: {}, Free slots: {}", gs.poolCount, (gs.totalBytes / 1024), gs.totalAllocated, gs.totalFree);

        for (const auto& [type, pool] : m_Pools) {
            auto stats = GetPoolStats(type);
            s_Log.Info("  {}: slot={}B, blocks={}, used={}/{}, ({}KB)", ResourceTypeName(type), stats.slotSize, stats.blockCount, stats.allocatedSlots, (stats.blockCount * stats.slotsPerBlock), (stats.totalBytes / 1024));
        }
        s_Log.Info("============================");
    }

} // namespace Engine
