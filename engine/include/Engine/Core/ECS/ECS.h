#pragma once

/**
 * @file ECS.h
 * @brief 轻量级 ECS 核心 — Entity-Component-System 实现
 *
 * 设计原则：
 *   - Entity = uint32 索引（带版本号），0 为空实体
 *   - Component = POD 结构体（纯数据）
 *   - System = 操作特定组件组合的逻辑
 *   - 稀疏集存储：O(1) 查询 + 紧凑迭代
 *
 * 使用示例：
 * @code
 *   EntityManager em;
 *   Entity e = em.Create();
 *   em.AddComponent<Transform>(e, Vec3{0,0,0}, Vec3{0,0,0}, Vec3{1,1,1});
 *   em.AddComponent<Sprite>(e, texture);
 *
 *   // 系统遍历
 *   em.ForEach<Transform, Sprite>([](Entity id, Transform& t, Sprite& s) {
 *       // 渲染逻辑
 *   });
 * @endcode
 */

#include "Engine/Types.h"
#include <vector>
#include <unordered_map>
#include <cassert>
#include <typeinfo>
#include <memory>
#include <functional>

namespace Engine {

    // ============================================================
    // Entity
    // ============================================================
    using Entity = uint32;
    constexpr Entity ENTITY_NULL = 0;

    // ============================================================
    // ComponentHandle — 兼容现有组件类的包装
    // ============================================================
    // 对于已经在代码中使用的组件类（TransformComponent, SpriteComponent 等），
    // 由 ComponentPool<T> 管理其存储。这些类仍然可以有成员函数。
    // 纯 ECS 组件推荐使用 POD 结构体。

    // ============================================================
    // ComponentPool — 类型化组件存储（稀疏集）
    // ============================================================
    template<typename T>
    class ComponentPool {
    public:
        // ── 增删查 ──

        T& Add(Entity entity, T&& component) {
            assert(!Has(entity) && "Component already exists on entity");
            EnsureCapacity(entity);

            int32 idx = static_cast<int32>(m_Components.size());
            m_IndexToEntity.push_back(entity);
            m_Components.push_back(std::move(component));
            m_EntityToIndex[entity] = idx;
            return m_Components.back();
        }

        T& Add(Entity entity, const T& component) {
            assert(!Has(entity) && "Component already exists on entity");
            EnsureCapacity(entity);

            int32 idx = static_cast<int32>(m_Components.size());
            m_IndexToEntity.push_back(entity);
            m_Components.push_back(component);
            m_EntityToIndex[entity] = idx;
            return m_Components.back();
        }

        template<typename... Args>
        T& Emplace(Entity entity, Args&&... args) {
            assert(!Has(entity) && "Component already exists on entity");
            EnsureCapacity(entity);

            int32 idx = static_cast<int32>(m_Components.size());
            m_IndexToEntity.push_back(entity);
            m_Components.emplace_back(std::forward<Args>(args)...);
            m_EntityToIndex[entity] = idx;
            return m_Components.back();
        }

        void Remove(Entity entity) {
            auto it = m_EntityToIndex.find(entity);
            if (it == m_EntityToIndex.end()) return;

            int32 idx = it->second;
            int32 lastIdx = static_cast<int32>(m_Components.size()) - 1;

            // 将要删除的元素与最后一个元素交换
            if (idx != lastIdx) {
                Entity lastEntity = m_IndexToEntity.back();
                m_Components[idx] = std::move(m_Components.back());
                m_IndexToEntity[idx] = lastEntity;
                m_EntityToIndex[lastEntity] = idx;
            }

            m_Components.pop_back();
            m_IndexToEntity.pop_back();
            m_EntityToIndex.erase(it);
        }

        T* Get(Entity entity) {
            auto it = m_EntityToIndex.find(entity);
            if (it == m_EntityToIndex.end()) return nullptr;
            return &m_Components[it->second];
        }

        const T* Get(Entity entity) const {
            auto it = m_EntityToIndex.find(entity);
            if (it == m_EntityToIndex.end()) return nullptr;
            return &m_Components[it->second];
        }

        bool Has(Entity entity) const {
            return m_EntityToIndex.find(entity) != m_EntityToIndex.end();
        }

        // ── 迭代 ──

        size_t Size() const { return m_Components.size(); }
        bool Empty() const { return m_Components.empty(); }

        const std::vector<T>& GetComponents() const { return m_Components; }
        const std::vector<Entity>& GetEntities() const { return m_IndexToEntity; }

        void Clear() {
            m_EntityToIndex.clear();
            m_IndexToEntity.clear();
            m_Components.clear();
        }

    private:
        void EnsureCapacity(Entity entity) {
            if (m_EntityToIndex.empty()) {
                m_EntityToIndex.reserve(1024);
                m_IndexToEntity.reserve(1024);
                m_Components.reserve(1024);
            }
            (void)entity;
        }

        std::unordered_map<Entity, int32> m_EntityToIndex;  // 稀疏映射
        std::vector<Entity>              m_IndexToEntity;    // 密集索引 → 实体
        std::vector<T>                   m_Components;       // 密集索引 → 组件数据
    };

    // ============================================================
    // EntityManager — 实体生命周期 + 组件管理
    // ============================================================
    class EntityManager {
    public:
        EntityManager() {
            // 预分配
            m_Entities.reserve(4096);
            m_Version.reserve(4096);
            // Entity 0 = null
            m_Entities.push_back(0);
            m_Version.push_back(0);
            m_FreeList.reserve(128);
        }

        ~EntityManager() { Clear(); }

        // ── 实体生命周期 ──

        /** 创建新实体，返回 Entity ID */
        Entity Create() {
            Entity id;
            if (!m_FreeList.empty()) {
                id = m_FreeList.back();
                m_FreeList.pop_back();
            } else {
                id = static_cast<Entity>(m_Entities.size());
                m_Entities.push_back(0);
                m_Version.push_back(1);
            }
            m_Entities[id] = id;
            return id;
        }

        /** 销毁实体及其所有组件 */
        void Destroy(Entity entity) {
            if (entity == ENTITY_NULL || !IsAlive(entity)) return;

            // 移除所有组件
            for (auto& [typeHash, pool] : m_ComponentPools) {
                pool->RemoveEntity(entity);
            }

            m_Entities[entity] = 0;
            m_Version[entity]++;
            m_FreeList.push_back(entity);
        }

        /** 实体是否存活 */
        bool IsAlive(Entity entity) const {
            return entity < m_Entities.size() && m_Entities[entity] == entity;
        }

        /** 获取实体版本号 */
        uint32 GetVersion(Entity entity) const {
            return entity < m_Version.size() ? m_Version[entity] : 0;
        }

        // ── 组件管理 ──

        template<typename T, typename... Args>
        T& AddComponent(Entity entity, Args&&... args) {
            assert(IsAlive(entity) && "Entity not alive");
            auto* pool = GetOrCreatePool<T>();
            return pool->Emplace(entity, std::forward<Args>(args)...);
        }

        template<typename T>
        void RemoveComponent(Entity entity) {
            assert(IsAlive(entity) && "Entity not alive");
            auto* pool = GetPool<T>();
            if (pool) pool->Remove(entity);
        }

        template<typename T>
        T* GetComponent(Entity entity) {
            if (!IsAlive(entity)) return nullptr;
            auto* pool = GetPool<T>();
            return pool ? pool->Get(entity) : nullptr;
        }

        template<typename T>
        const T* GetComponent(Entity entity) const {
            if (!IsAlive(entity)) return nullptr;
            auto* pool = GetPool<T>();
            return pool ? pool->Get(entity) : nullptr;
        }

        template<typename T>
        bool HasComponent(Entity entity) const {
            auto* pool = GetPool<T>();
            return pool && pool->Has(entity);
        }

        // ── 组件迭代 ──

        /**
         * @brief 遍历拥有指定组件组合的所有实体
         * @tparam Ts 需要同时拥有的组件类型列表
         * @param callback void(Entity, Ts&...)
         *
         * 示例：ForEach<Transform, Sprite>([](Entity e, Transform& t, Sprite& s) { ... })
         */
        template<typename... Ts, typename Func>
        void ForEach(Func&& callback) {
            if constexpr (sizeof...(Ts) == 0) return;

            // 遍历所有存活实体，检查组件组合
            Entity count = static_cast<Entity>(m_Entities.size());
            for (Entity e = 1; e < count; ++e) {
                if (!IsAlive(e)) continue;
                if ((HasComponent<Ts>(e) && ...)) {
                    callback(e, *GetComponent<Ts>(e)...);
                }
            }
        }

        // ── 清理 ──

        void Clear() {
            for (auto& [hash, pool] : m_ComponentPools) {
                (void)hash;
                pool->Clear();
            }
            m_Entities.assign(m_Entities.size(), 0);
            m_FreeList.clear();
        }

        size_t GetEntityCount() const {
            return m_Entities.size() - 1 - m_FreeList.size();
        }

    private:
        // ── 组件池类型擦除基类 ──
        struct IPool {
            virtual ~IPool() = default;
            virtual void RemoveEntity(Entity entity) = 0;
            virtual void Clear() = 0;
        };

        template<typename T>
        struct PoolWrapper : public IPool {
            ComponentPool<T> pool;

            void RemoveEntity(Entity entity) override {
                pool.Remove(entity);
            }

            void Clear() override {
                pool.Clear();
            }
        };

        template<typename T>
        ComponentPool<T>* GetPool() {
            auto it = m_ComponentPools.find(typeid(T).hash_code());
            if (it != m_ComponentPools.end())
                return &static_cast<PoolWrapper<T>*>(it->second.get())->pool;
            return nullptr;
        }

        template<typename T>
        const ComponentPool<T>* GetPool() const {
            auto it = m_ComponentPools.find(typeid(T).hash_code());
            if (it != m_ComponentPools.end())
                return &static_cast<const PoolWrapper<T>*>(it->second.get())->pool;
            return nullptr;
        }

        template<typename T>
        ComponentPool<T>* GetOrCreatePool() {
            auto* pool = GetPool<T>();
            if (!pool) {
                auto wrapper = std::make_unique<PoolWrapper<T>>();
                pool = &wrapper->pool;
                m_ComponentPools[typeid(T).hash_code()] = std::move(wrapper);
            }
            return pool;
        }

        // ── 数据 ──

        // 实体版本
        std::vector<Entity>  m_Entities;    // entity → entity (0 = dead)
        std::vector<uint32>  m_Version;     // entity → version
        std::vector<Entity>  m_FreeList;    // 可复用的实体 ID

        // 组件池（type_index → IPool*）
        std::unordered_map<size_t, std::unique_ptr<IPool>> m_ComponentPools;
    };

} // namespace Engine