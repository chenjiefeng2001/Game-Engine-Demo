#pragma once

/**
 * @file Chunk.h
 * @brief 16KB 固定大小内存块 — ECS 连续组件存储单元
 *
 * 每个 Chunk 为 archetype 中所有实体提供连续的组件数组存储。
 * 删除实体使用 swap-with-back 策略保持内存紧凑。
 *
 * 内存布局（以 3 组件 Archetype 为例）：
 *
 *   ┌───────────────┬──────────────┬────────────┬──────────────┐
 *   │ Transform[N]  │ Velocity[N]  │ Health[N]  │ EntityMap[N] │
 *   │ 32B each      │ 12B each     │ 4B each    │ 8B each      │
 *   │ offset=0      │ offset=4KB   │ offset=5.5KB │ offset=6KB │
 *   └───────────────┴──────────────┴────────────┴──────────────┘
 *
 * 总容量 N = min(每个组件的容量) ≈ (16384 - meta) / stride
 */

#include "Engine/Core/ECS/ECS.fwd.h"
#include <cstdint>
#include <cstring>
#include <cassert>
#include <span>

namespace Engine {

class Chunk {
public:
    Chunk() = default;
    ~Chunk();

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    // ── 初始化（由 Archetype 在创建 Chunk 时调用） ──
    void Initialize(
        const ComponentMeta* metas,
        uint32 componentCount,
        uint32 capacity
    );

    // ── 实体分配 ──
    /** 分配一个新行，返回行号 */
    uint32 AllocateRow();

    /** 释放指定行（swap-with-back，保持紧凑） */
    void RemoveRow(uint32 row);

    /** 当前实体数 */
    uint32 GetEntityCount() const { return m_EntityCount; }

    /** 是否已满 */
    bool IsFull() const { return m_EntityCount >= m_Capacity; }

    /** 是否为空的 Chunk */
    bool IsEmpty() const { return m_EntityCount == 0; }

    // ── 组件数据访问 ──

    /** 获取指定组件类型的连续数组起始指针 */
    void* GetComponentArrayPtr(ComponentTypeID typeID) const;

    /** 获取指定组件指定行的指针 */
    template<typename T>
    T* GetComponent(uint32 row) {
        assert(row < m_EntityCount);
        size_t idx = GetComponentTypeIndex(ComponentType<T>::ID());
        if (idx == SIZE_MAX) return nullptr;
        return reinterpret_cast<T*>(m_Data + m_OffsetTable[idx]) + row;
    }

    template<typename T>
    std::span<T> GetComponentSpan() {
        size_t idx = GetComponentTypeIndex(ComponentType<T>::ID());
        if (idx == SIZE_MAX) return {};
        return std::span<T>(
            reinterpret_cast<T*>(m_Data + m_OffsetTable[idx]),
            m_EntityCount
        );
    }

    // ── Entity 映射 ──

    /** 获取指定行的 EntityHandle */
    EntityHandle GetEntity(uint32 row) const {
        assert(row < m_EntityCount);
        return m_EntityMap[row];
    }

    /** 设置指定行的 EntityHandle */
    void SetEntity(uint32 row, EntityHandle entity) {
        assert(row < m_EntityCount);
        m_EntityMap[row] = entity;
    }

    const ComponentMeta* GetComponentMetas() const { return m_Metas; }
    uint32 GetComponentCount() const { return m_ComponentCount; }
    uint32 GetCapacity() const { return m_Capacity; }

private:
    /** 根据组件类型 ID 查找在该 Archetype 中的索引 */
    size_t GetComponentTypeIndex(ComponentTypeID typeID) const;

    /** 在原始数据指针上调用析构函数 */
    void DestructRow(uint32 row);

    // ── 数据 ──
    alignas(16) uint8 m_Data[kChunkSize];   // 16 字节对齐，支持 SIMD

    uint32 m_EntityCount   = 0;
    uint32 m_Capacity      = 0;
    uint32 m_ComponentCount = 0;

    // 组件元信息指针（指向 Archetype 持有的数组）
    const ComponentMeta* m_Metas = nullptr;

    // 组件偏移表：m_OffsetTable[i] = 组件 i 在 m_Data 中的字节偏移
    uint32 m_OffsetTable[kMaxComponentTypes];

    // 行号 → EntityHandle 映射
    EntityHandle m_EntityMap[kChunkMaxEntities];
};

} // namespace Engine