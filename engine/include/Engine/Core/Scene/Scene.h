#pragma once

#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/ECS/ECS.h"
#include "Engine/Core/ECS/System.h"
#include "Engine/Core/RHI/IRenderable.h"
#include <string>
#include <vector>
#include <memory>

namespace Engine {

    class IRenderQueue;

    /**
     * @brief 场景 — ECS 驱动，管理 EntityManager 和 System 列表
     *
     * Scene 是 ECS 架构的核心协调者：
     *   - 持有 EntityManager（实体/组件存储）
     *   - 持有 System 列表（依次执行 OnUpdate）
     *   - 同时维护 GameObject 句柄列表（向后兼容）
     */
    class Scene {
    public:
        Scene();
        ~Scene();

        // ── ECS 核心访问 ──
        EntityManager& GetEntityManager() noexcept { return m_EntityManager; }
        const EntityManager& GetEntityManager() const noexcept { return m_EntityManager; }

        /** 设置/获取活跃 EntityManager（供 GameObject 句柄访问） */
        static void SetActiveEntityManager(class EntityManager* em);
        static class EntityManager* GetActiveEntityManager();

        // ── System 管理 ──
        void AddSystem(std::unique_ptr<System> system);
        template<typename T, typename... Args>
        T* AddSystem(Args&&... args) {
            auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = ptr.get();
            m_Systems.push_back(std::move(ptr));
            return raw;
        }
        void ClearSystems();

        // ── 对象管理（向后兼容） ──
        void AddObject(std::shared_ptr<GameObject> obj);
        bool RemoveObject(GameObject* obj);
        void Clear();

        GameObject* FindObject(const std::string& name);
        const GameObject* FindObject(const std::string& name) const;

        // ── 生命周期 ──
        void OnCreate();
        void Update(float32 dt);
        void Render();
        void OnDestroy();
        void PostPhysicsUpdate();
        void CollectRenderCommands(IRenderQueue& queue);

        // ── 访问器 ──
        const std::vector<std::shared_ptr<GameObject>>& GetObjects() const noexcept {
            return m_Objects;
        }

    private:
        EntityManager m_EntityManager;

        std::vector<std::unique_ptr<System>> m_Systems;
        std::vector<std::shared_ptr<GameObject>> m_Objects;
    };

} // namespace Engine
