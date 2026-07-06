#include "Engine/Core/ECS/EntityManager.h"
#include "Engine/Core/ECS/ComponentRegistry.h"
#include <cassert>

namespace Engine {

EntityManager::EntityManager() {
    m_EntityPool = SparseSet<uint32>(1024);
}

EntityManager::~EntityManager() = default;

// ═══════════════════════════════════════════════════════════
// 实体生命周期
// ═══════════════════════════════════════════════════════════

EntityHandle EntityManager::CreateEntity() {
    uint32 index = m_EntityPool.Allocate();
    uint32 gen   = m_EntityPool.GetGeneration(index);

    // 确保 Locations 数组足够大
    if (index >= m_Locations.size()) {
        m_Locations.resize(index + 1);
    }

    m_Locations[index] = { nullptr, nullptr, 0 };

    return EntityHandle::Create(index, gen);
}

void EntityManager::DestroyEntity(EntityHandle entity) {
    if (!IsAlive(entity)) return;

    uint32 idx = entity.Index();
    EntityLocation& loc = m_Locations[idx];

    // 触发所有组件的移除回调（物理层用于清理 Jolt Body）
    if (m_OnComponentRemoved && loc.archetype != nullptr) {
        for (uint32 ci = 0; ci < loc.archetype->GetComponentCount(); ++ci) {
            const auto& meta = loc.archetype->GetComponentMetas()[ci];
            m_OnComponentRemoved(entity, meta.typeID);
        }
    }

    if (loc.archetype != nullptr) {
        // 从所属 Archetype 移除
        loc.archetype->RemoveEntity(loc.chunk, loc.row);
    }

    // 标记为空
    loc.archetype = nullptr;
    loc.chunk = nullptr;
    loc.row = 0;

    // 释放 ID
    m_EntityPool.Free(idx);
}

bool EntityManager::IsAlive(EntityHandle entity) const {
    if (entity.IsNull()) return false;
    uint32 idx = entity.Index();
    if (idx >= m_Locations.size()) return false;
    if (!m_EntityPool.IsAlive(idx)) return false;
    return m_EntityPool.GetGeneration(idx) == entity.Generation();
}

uint32 EntityManager::GetEntityCount() const {
    return static_cast<uint32>(m_EntityPool.Size());
}

Archetype* EntityManager::GetEntityArchetype(EntityHandle entity) const {
    if (!IsAlive(entity)) return nullptr;
    return m_Locations[entity.Index()].archetype;
}

// ═══════════════════════════════════════════════════════════
// Archetype 管理
// ═══════════════════════════════════════════════════════════

Archetype* EntityManager::FindOrCreateArchetype(const ComponentSignature& sig) {
    uint64 key = sig.to_ullong();

    auto it = m_ArchetypeMap.find(key);
    if (it != m_ArchetypeMap.end()) {
        return it->second.get();
    }

    // 收集该签名对应的组件元信息
    std::vector<ComponentMeta> metas;
    for (uint32 i = 0; i < kMaxComponentTypes; ++i) {
        if (sig.test(i)) {
            // 注意：需要外部提前注册所有组件类型的 Meta
            // 这里我们用一个全局注册表
            auto* meta = GetComponentMetaByTypeID(i);
            if (meta) {
                metas.push_back(*meta);
            }
        }
    }

    assert(!metas.empty() && "Cannot create Archetype with no components");

    auto archetype = std::make_unique<Archetype>(sig, std::move(metas));
    Archetype* raw = archetype.get();
    m_ArchetypeMap[key] = std::move(archetype);
    m_Archetypes.push_back(raw);
    m_ArchetypeVersion++;

    return raw;
}

// ═══════════════════════════════════════════════════════════
// 组件迁移
// ═══════════════════════════════════════════════════════════

void EntityManager::MigrateEntity(
    EntityHandle entity,
    Archetype* fromArch,
    Chunk* fromChunk,
    uint32 fromRow,
    Archetype* toArch
) {
    // 在目标 Archetype 中分配新行
    auto [toChunk, toRow] = toArch->AddEntity(entity);

    // 拷贝所有公共组件的数据
    for (uint32 i = 0; i < fromArch->GetComponentCount(); ++i) {
        const auto& meta = fromArch->GetComponentMetas()[i];
        int32 toIdx = toArch->GetComponentIndex(meta.typeID);
        if (toIdx >= 0) {
            void* srcPtr = fromChunk->GetComponentArrayPtr(meta.typeID);
            void* dstPtr = toChunk->GetComponentArrayPtr(meta.typeID);
            if (srcPtr && dstPtr) {
                // Move 构造函数
                meta.MoveConstruct(
                    static_cast<uint8*>(dstPtr) + meta.size * toRow,
                    static_cast<uint8*>(srcPtr) + meta.size * fromRow
                );
            }
        }
    }

    // 从旧 Archetype 移除
    fromArch->RemoveEntity(fromChunk, fromRow);

    // 触发被移除组件的回调
    if (m_OnComponentRemoved) {
        for (uint32 i = 0; i < fromArch->GetComponentCount(); ++i) {
            const auto& meta = fromArch->GetComponentMetas()[i];
            if (toArch->GetComponentIndex(meta.typeID) < 0) {
                m_OnComponentRemoved(entity, meta.typeID);
            }
        }
    }

    // 更新 Location
    EntityLocation& loc = m_Locations[entity.Index()];
    loc.archetype = toArch;
    loc.chunk = toChunk;
    loc.row = toRow;
}

// ═══════════════════════════════════════════════════════════
// 原始组件操作（供 ECB 和模板方法内部使用）
// ═══════════════════════════════════════════════════════════

void EntityManager::AddComponentRaw(
    EntityHandle entity, ComponentTypeID typeID, const void* data
) {
    if (!IsAlive(entity)) return;

    EntityLocation& loc = m_Locations[entity.Index()];
    ComponentSignature oldSig, newSig;

    if (loc.archetype != nullptr) {
        oldSig = loc.archetype->GetSignature();
    }
    newSig = oldSig;
    newSig.set(typeID);

    if (newSig == oldSig) {
        // 实体已经有该组件，无事可做
        return;
    }

    // 获取目标 Archetype
    Archetype* toArch = FindOrCreateArchetype(newSig);

    if (loc.archetype == nullptr) {
        // 实体尚无任何组件：直接添加到目标 Archetype
        auto [chunk, row] = toArch->AddEntity(entity);

        // 写入组件数据
        if (data) {
            void* ptr = chunk->GetComponentArrayPtr(typeID);
            if (ptr) {
                auto& meta = *GetComponentMetaByTypeID(typeID);
                std::memcpy(
                    static_cast<uint8*>(ptr) + meta.size * row,
                    data,
                    meta.size
                );
            }
        }

        loc.archetype = toArch;
        loc.chunk = chunk;
        loc.row = row;
    } else {
        // Archetype 迁移
        MigrateEntity(entity, loc.archetype, loc.chunk, loc.row, toArch);

        // 写入新组件数据
        auto& newLoc = m_Locations[entity.Index()];
        if (data) {
            void* ptr = newLoc.chunk->GetComponentArrayPtr(typeID);
            if (ptr) {
                auto& meta = *GetComponentMetaByTypeID(typeID);
                std::memcpy(
                    static_cast<uint8*>(ptr) + meta.size * newLoc.row,
                    data,
                    meta.size
                );
            }
        }
    }
}

void EntityManager::RemoveComponentRaw(EntityHandle entity, ComponentTypeID typeID) {
    if (!IsAlive(entity)) return;

    EntityLocation& loc = m_Locations[entity.Index()];
    if (loc.archetype == nullptr) return;

    ComponentSignature oldSig = loc.archetype->GetSignature();
    if (!oldSig.test(typeID)) return; // 没有该组件

    ComponentSignature newSig = oldSig;
    newSig.reset(typeID);

    if (newSig.none()) {
        // 触发组件移除回调（物理层清理 Jolt Body）
        if (m_OnComponentRemoved) {
            m_OnComponentRemoved(entity, typeID);
        }

        // 从旧 Archetype 移除，但保留实体存活（无组件实体）
        if (loc.archetype != nullptr) {
            loc.archetype->RemoveEntity(loc.chunk, loc.row);
        }
        loc.archetype = nullptr;
        loc.chunk = nullptr;
        loc.row = 0;
        return;
    }

    // 目标 Archetype
    Archetype* toArch = FindOrCreateArchetype(newSig);

    // 迁移（会自动跳过目标中没有的组件）
    MigrateEntity(entity, loc.archetype, loc.chunk, loc.row, toArch);
}

// ═══════════════════════════════════════════════════════════
// 查询
// ═══════════════════════════════════════════════════════════

QueryBuilder EntityManager::Query() {
    return QueryBuilder(this);
}

// ═══════════════════════════════════════════════════════════
// QueryBuilder::Build 实现
// ═══════════════════════════════════════════════════════════

ECSQuery QueryBuilder::Build() {
    ECSQuery query;

    for (Archetype* arch : m_Manager->m_Archetypes) {
        const auto& sig = arch->GetSignature();

        // 检查是否包含所有需要的组件
        bool allIncluded = true;
        for (uint32 i = 0; i < kMaxComponentTypes; ++i) {
            if (m_Include.test(i) && !sig.test(i)) {
                allIncluded = false;
                break;
            }
        }
        if (!allIncluded) continue;

        // 检查是否包含任何排除的组件
        bool anyExcluded = false;
        for (uint32 i = 0; i < kMaxComponentTypes; ++i) {
            if (m_Exclude.test(i) && sig.test(i)) {
                anyExcluded = true;
                break;
            }
        }
        if (anyExcluded) continue;

        // 匹配成功，添加所有 Chunk 的遍历范围
        for (uint32 ci = 0; ci < arch->GetChunkCount(); ++ci) {
            Chunk* chunk = arch->GetChunk(ci);
            if (chunk && !chunk->IsEmpty()) {
                query.m_Ranges.push_back({
                    chunk,
                    0,
                    chunk->GetEntityCount()
                });
            }
        }
    }

    return query;
}

} // namespace Engine