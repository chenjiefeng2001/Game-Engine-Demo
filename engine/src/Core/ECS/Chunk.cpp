#include "Engine/Core/ECS/Chunk.h"
#include <cstring>
#include <cassert>

namespace Engine {

// ── 析构：确保所有实体组件的析构函数被调用 ──
Chunk::~Chunk() {
    if (m_Metas == nullptr || m_EntityCount == 0) return;

    // 遍历每个组件类型，对每行调用析构函数
    for (uint32 row = 0; row < m_EntityCount; ++row) {
        DestructRow(row);
    }
}

// ── 初始化 ──
void Chunk::Initialize(
    const ComponentMeta* metas,
    uint32 componentCount,
    uint32 capacity
) {
    assert(metas != nullptr);
    assert(componentCount > 0);
    assert(componentCount <= kMaxComponentTypes);
    assert(capacity <= kChunkMaxEntities);

    m_Metas = metas;
    m_ComponentCount = componentCount;
    m_Capacity = capacity;
    m_EntityCount = 0;

    // 计算每个组件的偏移量
    uint32 offset = 0;
    for (uint32 i = 0; i < componentCount; ++i) {
        // 16 字节对齐（SIMD 友好）
        offset = (offset + metas[i].alignment - 1) & ~(metas[i].alignment - 1);
        m_OffsetTable[i] = offset;
        offset += metas[i].size * capacity;
    }

    // 确保 EntityMap 在数据之后
    // EntityMap 存储在 m_Data 之后的固定区域
    assert(offset + sizeof(EntityHandle) * capacity <= kChunkSize);
}

// ── 分配新行 ──
uint32 Chunk::AllocateRow() {
    assert(!IsFull());
    uint32 row = m_EntityCount++;
    // 新行内存不需要清零，调用方会在 AddComponent 时写入
    return row;
}

// ── 删除行（swap-with-back） ──
void Chunk::RemoveRow(uint32 row) {
    assert(row < m_EntityCount);

    uint32 lastRow = m_EntityCount - 1;

    if (row != lastRow) {
        // 将最后一行移动到被删除的位置
        for (uint32 i = 0; i < m_ComponentCount; ++i) {
            const auto& meta = m_Metas[i];
            void* dstPtr = m_Data + m_OffsetTable[i] + meta.size * row;
            void* srcPtr = m_Data + m_OffsetTable[i] + meta.size * lastRow;

            // MoveConstruct: 在 dst 位置构造 src 的移动副本
            meta.MoveConstruct(dstPtr, srcPtr);
            // Destruct: 销毁 src 位置的原始对象
            meta.Destruct(srcPtr);
        }
        // 拷贝 EntityMap
        m_EntityMap[row] = m_EntityMap[lastRow];
    } else {
        // 最后一行：直接析构
        DestructRow(row);
    }

    m_EntityCount--;
}

// ── 析构指定行的所有组件 ──
void Chunk::DestructRow(uint32 row) {
    assert(row < m_EntityCount);
    for (uint32 i = 0; i < m_ComponentCount; ++i) {
        const auto& meta = m_Metas[i];
        void* ptr = m_Data + m_OffsetTable[i] + meta.size * row;
        meta.Destruct(ptr);
    }
}

// ── 获取组件类型对应的数组基址 ──
void* Chunk::GetComponentArrayPtr(ComponentTypeID typeID) const {
    for (uint32 i = 0; i < m_ComponentCount; ++i) {
        if (m_Metas[i].typeID == typeID) {
            return m_Data + m_OffsetTable[i];
        }
    }
    return nullptr;
}

// ── 查找组件类型在偏移表中的索引 ──
size_t Chunk::GetComponentTypeIndex(ComponentTypeID typeID) const {
    for (uint32 i = 0; i < m_ComponentCount; ++i) {
        if (m_Metas[i].typeID == typeID) {
            return i;
        }
    }
    return SIZE_MAX;
}

} // namespace Engine