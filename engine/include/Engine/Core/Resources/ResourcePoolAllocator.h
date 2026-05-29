#pragma once

/**
 * @file ResourcePoolAllocator.h
 * @brief 基于池/块的资源分配器 — 预分配固定块、O(1) 分配释放
 *
 * 设计要点：
 *   - 每个 ResourceType 对应一个独立池
 *   - 每个池预分配固定大小的块（block），每个块包含多个槽（slot）
 *   - 分配 O(1)（从空闲链表取头节点）
 *   - 释放 O(1)（将节点放回空闲链表）
 *   - 支持批量清理和内存统计
 *
 * 使用示例：
 * @code
 *   ResourcePoolAllocator allocator;
 *   allocator.CreatePool(ResourceType::Texture, sizeof(Texture), 64);
 *   void* mem = allocator.Allocate(ResourceType::Texture);
 *   Texture* tex = new (mem) Texture(...);
 *   tex->~Texture();
 *   allocator.Deallocate(ResourceType::Texture, mem);
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/Resources/Resource.h"
#include <vector>
#include <unordered_map>
#include <mutex>

namespace Engine {

    // ============================================================
    // 池配置
    // ============================================================
    struct PoolConfig {
        size_t slotSize     = 0;     // 每个槽的大小（bytes）
        size_t blockSize    = 4096;  // 每个块的大小（bytes，需能被 slotSize 整除）
        size_t maxBlocks    = 0;     // 最大块数（0 = 无限制）
        size_t alignment    = alignof(std::max_align_t);  // 对齐要求
    };

    // ============================================================
    // 池统计
    // ============================================================
    struct PoolStats {
        size_t slotSize        = 0;
        size_t slotsPerBlock   = 0;
        size_t blockCount      = 0;
        size_t allocatedSlots  = 0;
        size_t freeSlots       = 0;
        size_t totalBytes      = 0;
        size_t usedBytes       = 0;
        size_t peakAllocated   = 0;
    };

    // ============================================================
    // ResourcePoolAllocator
    // ============================================================
    class ResourcePoolAllocator {
    public:
        ResourcePoolAllocator() = default;
        ~ResourcePoolAllocator();

        ResourcePoolAllocator(const ResourcePoolAllocator&) = delete;
        ResourcePoolAllocator& operator=(const ResourcePoolAllocator&) = delete;

        // ── 池管理 ──

        /**
         * @brief 为指定资源类型创建内存池
         * @param type      资源类型
         * @param slotSize  每个槽的大小（sizeof 资源类型）
         * @param initialSlots 初始分配的槽数
         * @param alignment 对齐要求（默认 max_align_t）
         */
        void CreatePool(ResourceType type, size_t slotSize,
                        size_t initialSlots = 32,
                        size_t alignment = alignof(std::max_align_t));

        /** 销毁指定类型的池 */
        void DestroyPool(ResourceType type);

        /** 是否存在指定类型的池 */
        bool HasPool(ResourceType type) const;

        // ── 分配 / 释放 ──

        /**
         * @brief 从池中分配一块内存
         * @param type 资源类型
         * @return 对齐后的内存指针，失败返回 nullptr
         */
        void* Allocate(ResourceType type);

        /**
         * @brief 将内存归还池中
         * @param type 资源类型
         * @param ptr 由 Allocate 返回的指针
         */
        void Deallocate(ResourceType type, void* ptr);

        // ── 批量操作 ──

        /** 释放池中所有未使用的块（缩减内存占用） */
        void Trim();

        /** 释放所有池的所有内存 */
        void Clear();

        // ── 统计 ──

        /** 获取指定池的统计信息 */
        PoolStats GetPoolStats(ResourceType type) const;

        /** 获取所有池的总统计 */
        struct GlobalStats {
            size_t totalBytes      = 0;
            size_t totalAllocated  = 0;
            size_t totalFree       = 0;
            size_t poolCount       = 0;
        };
        GlobalStats GetGlobalStats() const;

        /** 打印所有池的统计到控制台 */
        void LogStats() const;

        /** @brief 将分配器状态转储到字符串（供崩溃报告使用） */
        std::string DumpState() const;

    private:
        // ── 内部结构 ──

        struct FreeNode {
            FreeNode* next = nullptr;
        };

        struct Block {
            char*    memory  = nullptr;
            size_t   size    = 0;
            bool     isFull  = false;

            Block() = default;
            ~Block() { delete[] memory; memory = nullptr; }

            Block(const Block&) = delete;
            Block& operator=(const Block&) = delete;

            Block(Block&& other) noexcept
                : memory(other.memory), size(other.size), isFull(other.isFull) {
                other.memory = nullptr;
                other.size = 0;
                other.isFull = false;
            }
            Block& operator=(Block&& other) noexcept {
                if (this != &other) {
                    delete[] memory;
                    memory = other.memory;
                    size = other.size;
                    isFull = other.isFull;
                    other.memory = nullptr;
                    other.size = 0;
                    other.isFull = false;
                }
                return *this;
            }
        };

        struct Pool {
            PoolConfig   config;
            FreeNode*    freeList    = nullptr;  // 空闲链表头
            std::vector<Block> blocks;
            size_t       allocatedCount = 0;      // 已分配槽数
            size_t       peakAllocated  = 0;      // 峰值已分配槽数

            ~Pool() { ClearBlocks(); }

            Pool() = default;
            Pool(const Pool&) = delete;
            Pool& operator=(const Pool&) = delete;
            Pool(Pool&& other) noexcept
                : config(other.config), freeList(other.freeList),
                  blocks(std::move(other.blocks)),
                  allocatedCount(other.allocatedCount),
                  peakAllocated(other.peakAllocated) {
                other.freeList = nullptr;
                other.allocatedCount = 0;
                other.peakAllocated = 0;
            }
            Pool& operator=(Pool&& other) noexcept {
                if (this != &other) {
                    ClearBlocks();
                    config = other.config;
                    freeList = other.freeList;
                    blocks = std::move(other.blocks);
                    allocatedCount = other.allocatedCount;
                    peakAllocated = other.peakAllocated;
                    other.freeList = nullptr;
                    other.allocatedCount = 0;
                    other.peakAllocated = 0;
                }
                return *this;
            }

            void ClearBlocks() {
                freeList = nullptr;
                blocks.clear();
                allocatedCount = 0;
                peakAllocated = 0;
            }
        };

        /** 为池分配一个新的块 */
        void AllocateNewBlock(Pool& pool);

        // ── 数据 ──
        std::unordered_map<ResourceType, Pool> m_Pools;
        mutable std::mutex m_Mutex;
    };

} // namespace Engine
