#pragma once

/**
 * @file EntityManager.h
 * @brief ECS 核心管理器 — 实体生命周期 + Archetype 管理 + 查询
 *
 * 核心职责：
 *   1. 实体创建/销毁（64-bit Generational ID）
 *   2. 组件添加/删除（Archetype 迁移）
 *   3. Archetype 注册与查找
 *   4. 查询执行
 *
 * 线程安全：所有结构性变更通过 EntityCommandBuffer 延迟执行。
 */

#include "Engine/Core/ECS/ECS.fwd.h"
#include "Engine/Core/ECS/SparseSet.h"
#include "Engine/Core/ECS/Archetype.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Engine {

class EntityManager {
public:
    EntityManager();
    ~EntityManager();

    // ═══════════════════════════════════════════════════
    // 实体生命周期
    // ═══════════════════════════════════════════════════

    /** 创建新实体（无组件，返回 EntityHandle） */
    EntityHandle CreateEntity();

    /** 销毁实体（移除所有组件并回收 ID） */
    void DestroyEntity(EntityHandle entity);

    /** 检查实体是否存活 */
    bool IsAlive(EntityHandle entity) const;

    /** 获取活跃实体总数 */
    uint32 GetEntityCount() const;

    // ═══════════════════════════════════════════════════
    // 组件操作
    // ═══════════════════════════════════════════════════

    /** 添加组件（执行 Archetype 迁移） */
    template<typename T, typename... Args>
    T& AddComponent(EntityHandle entity, Args&&... args);

    /** 移除组件 */
    template<typename T>
    void RemoveComponent(EntityHandle entity);

    /** 获取组件指针（若不存在返回 nullptr） */
    template<typename T>
    T* GetComponent(EntityHandle entity);

    template<typename T>
    const T* GetComponent(EntityHandle entity) const;

    /** 检查实体是否拥有指定组件 */
    template<typename T>
    bool HasComponent(EntityHandle entity) const;

    /** 获取实体当前所属的 Archetype */
    Archetype* GetEntityArchetype(EntityHandle entity) const;

    // ═══════════════════════════════════════════════════
    // 查询
    // ═══════════════════════════════════════════════════

    /** 构建查询迭代器 */
    class QueryBuilder Query();

    // ═══════════════════════════════════════════════════
    // 组件生命周期回调（用于物理层清理 Jolt Body 等）
    // ═══════════════════════════════════════════════════

    /** 组件被移除时的回调函数类型 */
    using ComponentRemovedCallback = std::function<void(EntityHandle, ComponentTypeID)>;

    /**
     * @brief 设置组件移除回调
     * @param cb 回调函数，参数为 (EntityHandle, ComponentTypeID)
     *
     * 当实体的组件被移除时（通过 RemoveComponent/RemoveComponentRaw/DestroyEntity），
     * 此回调会被触发。PhysicsSyncSystem 使用此回调清理 Jolt Body。
     */
    void SetComponentRemovedCallback(ComponentRemovedCallback cb) {
        m_OnComponentRemoved = std::move(cb);
    }

    // ═══════════════════════════════════════════════════
    // 内部工具（供 EntityCommandBuffer 使用）
    // ═══════════════════════════════════════════════════

    /** 原始添加组件（由 ECB Playback 调用） */
    void AddComponentRaw(EntityHandle entity, ComponentTypeID typeID, const void* data);

    /** 原始移除组件（由 ECB Playback 调用） */
    void RemoveComponentRaw(EntityHandle entity, ComponentTypeID typeID);

    // ── 调试 ──
    size_t GetArchetypeCount() const { return m_Archetypes.size(); }

private:
    // ── 实体位置数据 ──
    struct EntityLocation {
        Archetype* archetype = nullptr;
        Chunk*     chunk     = nullptr;
        uint32     row       = 0;
    };

    // ── 组件迁移：将实体从旧 Archetype 迁移到新 Archetype ──
    void MigrateEntity(
        EntityHandle entity,
        Archetype* fromArch,
        Chunk* fromChunk,
        uint32 fromRow,
        Archetype* toArch
    );

    // ── 根据组件签名查找或创建 Archetype ──
    Archetype* FindOrCreateArchetype(const ComponentSignature& sig);

    // ── 数据 ──
    SparseSet<uint32> m_EntityPool;       // Index → Generation (packed)
    std::vector<EntityLocation> m_Locations;  // Index → EntityLocation

    // Signature → Archetype
    std::unordered_map<uint64, std::unique_ptr<Archetype>> m_ArchetypeMap;

    // 所有 Archetype 的平铺列表（用于查询）
    std::vector<Archetype*> m_Archetypes;

    // 注册计数（用于 Query 缓存刷新）
    uint32 m_ArchetypeVersion = 0;

    // 组件移除回调
    ComponentRemovedCallback m_OnComponentRemoved;

    friend class QueryBuilder;
};

// ═══════════════════════════════════════════════════════
// 查询构建器
// ═══════════════════════════════════════════════════════

class QueryBuilder {
public:
    QueryBuilder(EntityManager* em) : m_Manager(em) {}

    /** 要求包含的组件 */
    template<typename T>
    QueryBuilder& With();

    /** 要求排除的组件 */
    template<typename T>
    QueryBuilder& Without();

    /** 构建查询 */
    class ECSQuery Build();

private:
    EntityManager* m_Manager;
    ComponentSignature m_Include;
    ComponentSignature m_Exclude;
};

// ═══════════════════════════════════════════════════════
// 查询迭代器
// ═══════════════════════════════════════════════════════

class ECSQuery {
public:
    ECSQuery() = default;

    struct ChunkRange {
        Chunk* chunk;
        uint32 startRow;
        uint32 count;
    };

    struct Iterator {
        const std::vector<ChunkRange>* ranges;
        size_t index;

        const ChunkRange& operator*() const { return (*ranges)[index]; }
        const ChunkRange* operator->() const { return &(*ranges)[index]; }
        Iterator& operator++() { ++index; return *this; }
        bool operator!=(const Iterator& o) const { return index != o.index; }
    };

    Iterator begin() const { return { &m_Ranges, 0 }; }
    Iterator end()   const { return { &m_Ranges, m_Ranges.size() }; }

    bool IsValid() const { return !m_Ranges.empty(); }
    size_t Size() const { return m_Ranges.size(); }

private:
    friend class QueryBuilder;
    std::vector<ChunkRange> m_Ranges;
    uint32 m_Version = 0;
};

// ═══════════════════════════════════════════════════════
// 模板实现（inline，方便编译器内联）
// ═══════════════════════════════════════════════════════

template<typename T, typename... Args>
T& EntityManager::AddComponent(EntityHandle entity, Args&&... args) {
    // 创建组件数据
    T component(std::forward<Args>(args)...);
    AddComponentRaw(entity, ComponentType<T>::ID(), &component);
    return *GetComponent<T>(entity);
}

template<typename T>
void EntityManager::RemoveComponent(EntityHandle entity) {
    RemoveComponentRaw(entity, ComponentType<T>::ID());
}

template<typename T>
T* EntityManager::GetComponent(EntityHandle entity) {
    if (!IsAlive(entity)) return nullptr;

    EntityLocation& loc = m_Locations[entity.Index()];
    if (loc.archetype == nullptr) return nullptr;

    return loc.chunk->GetComponent<T>(loc.row);
}

template<typename T>
const T* EntityManager::GetComponent(EntityHandle entity) const {
    return const_cast<EntityManager*>(this)->GetComponent<T>(entity);
}

template<typename T>
bool EntityManager::HasComponent(EntityHandle entity) const {
    if (!IsAlive(entity)) return false;
    const EntityLocation& loc = m_Locations[entity.Index()];
    if (loc.archetype == nullptr) return false;
    return loc.archetype->GetComponentIndex(ComponentType<T>::ID()) >= 0;
}

template<typename T>
QueryBuilder& QueryBuilder::With() {
    m_Include.set(ComponentType<T>::ID());
    return *this;
}

template<typename T>
QueryBuilder& QueryBuilder::Without() {
    m_Exclude.set(ComponentType<T>::ID());
    return *this;
}

} // namespace Engine