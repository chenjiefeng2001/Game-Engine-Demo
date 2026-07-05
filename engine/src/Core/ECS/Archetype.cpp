#include "Engine/Core/ECS/Archetype.h"
#include <cassert>
#include <algorithm>

namespace Engine {

// ── 构造 ──
Archetype::Archetype(ComponentSignature signature, std::vector<ComponentMeta> metas)
    : m_Signature(signature)
    , m_Metas(std::move(metas))
{
    assert(!m_Metas.empty() && "Archetype must have at least one component");
}

// ── 创建 Chunk ──
Chunk* Archetype::CreateChunk() {
    // 计算容量：取最小组件达到 kChunkSize 的最大行数
    uint32 maxStride = 0;
    for (const auto& meta : m_Metas) {
        uint32 stride = static_cast<uint32>(
            (meta.size + meta.alignment - 1) & ~(meta.alignment - 1)
        );
        if (stride > maxStride) maxStride = stride;
    }

    // 估算容量，预留 EntityMap 空间
    uint32 componentStride = 0;
    for (const auto& meta : m_Metas) {
        componentStride += static_cast<uint32>(
            (meta.size + meta.alignment - 1) & ~(meta.alignment - 1)
        );
    }

    // 预留 EntityMap 空间: kChunkMaxEntities * sizeof(EntityHandle)
    uint32 entityMapSize = kChunkMaxEntities * sizeof(EntityHandle);
    uint32 dataSize = kChunkSize - entityMapSize;
    uint32 capacity = dataSize / (componentStride + sizeof(EntityHandle));
    if (capacity > kChunkMaxEntities) capacity = kChunkMaxEntities;
    if (capacity == 0) capacity = 1;

    auto chunk = std::make_unique<Chunk>();
    chunk->Initialize(m_Metas.data(), static_cast<uint32>(m_Metas.size()), capacity);
    Chunk* raw = chunk.get();
    m_Chunks.push_back(std::move(chunk));
    return raw;
}

// ── 添加实体 ──
Archetype::EntityLocation Archetype::AddEntity(EntityHandle entity) {
    Chunk* chunk = GetOrCreateChunk();
    uint32 row = chunk->AllocateRow();
    chunk->SetEntity(row, entity);
    return { chunk, row };
}

// ── 移除实体 ──
void Archetype::RemoveEntity(Chunk* chunk, uint32 row) {
    assert(chunk != nullptr);
    chunk->RemoveRow(row);
}

// ── 获取有空位的 Chunk ──
Chunk* Archetype::GetOrCreateChunk() {
    // 从后向前查找有空位的 Chunk
    for (auto it = m_Chunks.rbegin(); it != m_Chunks.rend(); ++it) {
        if (!(*it)->IsFull()) {
            return it->get();
        }
    }
    return CreateChunk();
}

// ── 总实体数 ──
uint32 Archetype::GetEntityCount() const {
    uint32 count = 0;
    for (const auto& chunk : m_Chunks) {
        count += chunk->GetEntityCount();
    }
    return count;
}

// ── 获取组件类型对应的本地索引 ──
int32 Archetype::GetComponentIndex(ComponentTypeID typeID) const {
    for (size_t i = 0; i < m_Metas.size(); ++i) {
        if (m_Metas[i].typeID == typeID) {
            return static_cast<int32>(i);
        }
    }
    return -1;
}

// ── 查找实体 ──
bool Archetype::Contains(EntityHandle entity) const {
    for (const auto& chunk : m_Chunks) {
        for (uint32 row = 0; row < chunk->GetEntityCount(); ++row) {
            if (chunk->GetEntity(row) == entity) {
                return true;
            }
        }
    }
    return false;
}

} // namespace Engine