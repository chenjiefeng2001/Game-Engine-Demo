#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/ECS/System.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include "Engine/Core/RHI/IRenderQueue.h"
#include <algorithm>
#include <functional>

namespace Engine {

    Scene::Scene() {
        SetActiveEntityManager(&m_EntityManager);
    }

    Scene::~Scene() {
        OnDestroy();
        if (GetActiveEntityManager() == &m_EntityManager)
            SetActiveEntityManager(nullptr);
        m_Objects.clear();
    }

    // ──────────────────────────────────────────────
    // System 管理
    // ──────────────────────────────────────────────

    void Scene::AddSystem(std::unique_ptr<System> system) {
        if (system) m_Systems.push_back(std::move(system));
    }

    void Scene::ClearSystems() {
        m_Systems.clear();
    }

    // ──────────────────────────────────────────────
    // 对象管理（向后兼容）
    // ──────────────────────────────────────────────

    void Scene::AddObject(std::shared_ptr<GameObject> obj) {
        if (!obj) return;
        auto it = std::find(m_Objects.begin(), m_Objects.end(), obj);
        if (it != m_Objects.end()) return;

        // 创建 ECS Entity 并关联
        Entity entity = m_EntityManager.Create();
        obj->SetEntity(entity);
        m_EntityManager.AddComponent<TransformComponent>(entity);

        m_Objects.push_back(std::move(obj));
    }

    bool Scene::RemoveObject(GameObject* obj) {
        if (!obj) return false;
        auto it = std::find_if(m_Objects.begin(), m_Objects.end(),
            [obj](const std::shared_ptr<GameObject>& o) { return o.get() == obj; });
        if (it == m_Objects.end()) return false;
        if (obj->GetEntity() != ENTITY_NULL)
            m_EntityManager.Destroy(obj->GetEntity());
        m_Objects.erase(it);
        return true;
    }

    void Scene::Clear() {
        OnDestroy();
        m_EntityManager.Clear();
        m_Objects.clear();
    }

    // ──────────────────────────────────────────────
    // 查找
    // ──────────────────────────────────────────────

    namespace detail {
        // 递归查找（非 const 版本）
        GameObject* findRecursive(const std::string& name,
                                  const std::vector<std::shared_ptr<GameObject>>& objects) {
            for (auto& obj : objects) {
                if (obj->GetName() == name)
                    return obj.get();

                auto* found = findRecursive(name, obj->GetChildren());
                if (found)
                    return found;
            }
            return nullptr;
        }

        // 递归查找（const 版本）
        const GameObject* findRecursiveConst(
                const std::string& name,
                const std::vector<std::shared_ptr<GameObject>>& objects) {
            for (const auto& obj : objects) {
                if (obj->GetName() == name)
                    return obj.get();

                auto* found = findRecursiveConst(name, obj->GetChildren());
                if (found)
                    return found;
            }
            return nullptr;
        }
    } // namespace detail

    GameObject* Scene::FindObject(const std::string& name) {
        return detail::findRecursive(name, m_Objects);
    }

    const GameObject* Scene::FindObject(const std::string& name) const {
        return detail::findRecursiveConst(name, m_Objects);
    }

    // ──────────────────────────────────────────────
    // 生命周期 — ECS System 驱动
    // ──────────────────────────────────────────────

    void Scene::OnCreate() {
        std::function<void(GameObject&)> recCreate = [&](GameObject& o) {
            for (auto& c : o.GetChildren()) recCreate(*c);
        };
        for (auto& obj : m_Objects) recCreate(*obj);
    }

    void Scene::Update(float32 dt) {
        std::sort(m_Systems.begin(), m_Systems.end(),
            [](const auto& a, const auto& b) {
                return a->GetPriority() < b->GetPriority();
            });
        for (auto& sys : m_Systems) {
            sys->OnUpdate(m_EntityManager, dt);
        }
    }

    void Scene::Render() {
        for (auto& sys : m_Systems) {
            sys->OnRender(m_EntityManager);
        }
    }

    void Scene::OnDestroy() {
        std::function<void(GameObject&)> recDestroy = [&](GameObject& o) {
            for (auto& c : o.GetChildren()) recDestroy(*c);
        };
        for (auto& obj : m_Objects) recDestroy(*obj);
    }

    // ──────────────────────────────────────────────
    // 物理同步（ECS ForEach 方式）
    // ──────────────────────────────────────────────

    void Scene::PostPhysicsUpdate() {
        m_EntityManager.ForEach<TransformComponent, PhysicsComponent>(
            [](Entity entity, TransformComponent& transform, PhysicsComponent& physics) {
                (void)entity;
                Vec2 pos; float32 angle;
                physics.SyncPhysicsToTransform(pos, angle);
                transform.SetPosition(pos.x, pos.y, 0.0f);
                transform.SetRotation(0.0f, 0.0f, angle);
            }
        );
    }

    void Scene::CollectRenderCommands(IRenderQueue& queue) {
        for (auto& obj : m_Objects) {
            obj->CollectRenderCommands(queue);
            std::function<void(GameObject&)> collectRecursive =
                [&](GameObject& obj2) {
                    for (auto& child : obj2.GetChildren()) {
                        child->CollectRenderCommands(queue);
                        collectRecursive(*child);
                    }
                };
            collectRecursive(*obj);
        }
    }

} // namespace Engine
