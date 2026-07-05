#pragma once

/**
 * @file Archetype.h
 * @brief Archetype — 具有相同组件签名的实体集合
 *
 * 每个 Archetype 对应一组唯一的组件类型组合（由 ComponentSignature 标记）。
 * 实体的 AddComponent/RemoveComponent 操作会导致 Archetype 迁移：
 *   旧 Archetype → 移除实体 → 新 Archetype → 添加实体
 */

#include "Engine/Core/ECS/ECS.fwd.h"
#include "Engine/Core/ECS/Chunk.h"
#include <vector>
#include <memory>
#include <cstdint>

namespace Engine {

class Archetype {
public:
    // ── 构造 ──
    Archetype() = default;
    Archetype(ComponentSignature signature, std::vector<ComponentMeta> metas);

    // ── 签名 ──
    const ComponentSignature& GetSignature() const { return m_Signature; }

    // ── 实体操作 ──
    /** 添加一个实体（分配行，写入 EntityHandle） */
    struct EntityLocation {
        Chunk* chunk;
        uint32 row;
    };
    EntityLocation AddEntity(EntityHandle entity);

    /** 移除一个实体（该实体必须在此 Archetype 中） */
    void RemoveEntity(Chunk* chunk, uint32 row);

    /** 查找实体是否在此 Archetype */
    bool Contains(EntityHandle entity) const;

    // ── Chunk 管理 ──

    uint32 GetChunkCount() const { return static_cast<uint32>(m_Chunks.size()); }
    Chunk* GetChunk(uint32 index) { return m_Chunks[index].get(); }
    const Chunk* GetChunk(uint32 index) const { return m_Chunks[index].get(); }

    /** 获取一个有空位的 Chunk，若无则创建新块 */
    Chunk* GetOrCreateChunk();

    /** 总实体数 */
    uint32 GetEntityCount() const;

    // ── 组件元信息 ──

    const std::vector<ComponentMeta>& GetComponentMetas() const { return m_Metas; }
    uint32 GetComponentCount() const { return static_cast<uint32>(m_Metas.size()); }

    /** 获取组件类型对应的本地索引 */
    int32 GetComponentIndex(ComponentTypeID typeID) const;

private:
    // 创建新 Chunk
    Chunk* CreateChunk();

    // ── 数据 ──
    ComponentSignature m_Signature;
    std::vector<ComponentMeta> m_Metas;
    std::vector<std::unique_ptr<Chunk>> m_Chunks;
};

} // namespace Engine