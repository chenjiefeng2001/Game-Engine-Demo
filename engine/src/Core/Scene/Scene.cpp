#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Scene/Serializer.h"
#include "Engine/Core/RHI/IRenderQueue.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include <algorithm>
#include <functional>

namespace Engine {

    // ──────────────────────────────────────────────
    // 构造 / 析构
    // ──────────────────────────────────────────────

    Scene::Scene()
        : m_Name("Scene") {
    }

    Scene::Scene(std::string name)
        : m_Name(std::move(name)) {
    }

    Scene::~Scene() {
        OnDestroy();
        m_Objects.clear();
    }

    // ──────────────────────────────────────────────
    // 对象管理
    // ──────────────────────────────────────────────

    void Scene::AddObject(std::shared_ptr<GameObject> obj) {
        if (!obj) return;
        auto it = std::find(m_Objects.begin(), m_Objects.end(), obj);
        if (it != m_Objects.end()) return;
        m_Objects.push_back(std::move(obj));
    }

    bool Scene::RemoveObject(GameObject* obj) {
        if (!obj) return false;
        auto it = std::find_if(m_Objects.begin(), m_Objects.end(),
            [obj](const std::shared_ptr<GameObject>& o) { return o.get() == obj; });
        if (it == m_Objects.end()) return false;
        (*it)->OnDestroy();
        m_Objects.erase(it);
        return true;
    }

    void Scene::Clear() {
        OnDestroy();
        m_Objects.clear();
    }

    bool Scene::Contains(GameObject* obj) const {
        if (!obj) return false;
        return std::any_of(m_Objects.begin(), m_Objects.end(),
            [obj](const std::shared_ptr<GameObject>& o) { return o.get() == obj; });
    }

    size_t Scene::GetTotalObjectCount() const {
        size_t count = 0;
        std::function<void(const std::vector<std::shared_ptr<GameObject>>&)> recCount;
        recCount = [&](const auto& list) {
            for (auto& obj : list) {
                ++count;
                recCount(obj->GetChildren());
            }
        };
        recCount(m_Objects);
        return count;
    }

    // ──────────────────────────────────────────────
    // 查找
    // ──────────────────────────────────────────────

    namespace detail {
        GameObject* findRecursive(const std::string& name,
                                  const std::vector<std::shared_ptr<GameObject>>& objects) {
            for (auto& obj : objects) {
                if (obj->GetName() == name) return obj.get();
                auto* found = findRecursive(name, obj->GetChildren());
                if (found) return found;
            }
            return nullptr;
        }

        const GameObject* findRecursiveConst(
                const std::string& name,
                const std::vector<std::shared_ptr<GameObject>>& objects) {
            for (const auto& obj : objects) {
                if (obj->GetName() == name) return obj.get();
                auto* found = findRecursiveConst(name, obj->GetChildren());
                if (found) return found;
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
    // 生命周期（非活跃场景直接跳过）
    // ──────────────────────────────────────────────

    void Scene::OnCreate() {
        if (!m_Active) return;
        for (auto& obj : m_Objects) {
            obj->OnCreate();
            std::function<void(GameObject&)> recCreate =
                [&](GameObject& o) {
                    for (auto& c : o.GetChildren()) {
                        c->OnCreate();
                        recCreate(*c);
                    }
                };
            recCreate(*obj);
        }
    }

    void Scene::Update(float32 dt) {
        if (!m_Active) return;
        for (auto& obj : m_Objects) {
            obj->Update(dt);
        }
    }

    void Scene::Render() {
        if (!m_Active) return;
        for (auto& obj : m_Objects) {
            obj->Render();
            std::function<void(GameObject&)> recRender =
                [&](GameObject& o) {
                    for (auto& c : o.GetChildren()) {
                        c->Render();
                        recRender(*c);
                    }
                };
            recRender(*obj);
        }
    }

    void Scene::OnDestroy() {
        for (auto& obj : m_Objects) {
            obj->OnDestroy();
            std::function<void(GameObject&)> recDestroy =
                [&](GameObject& o) {
                    for (auto& c : o.GetChildren()) {
                        c->OnDestroy();
                        recDestroy(*c);
                    }
                };
            recDestroy(*obj);
        }
    }

    // ──────────────────────────────────────────────
    // 物理同步
    // ──────────────────────────────────────────────

    namespace detail {
        static void PostPhysicsUpdateRecursive(GameObject& obj) {
            if (obj.HasPhysics()) {
                Vec2 pos;
                float32 angle;
                obj.GetPhysics().SyncPhysicsToTransform(pos, angle);
                obj.GetTransform().SetPosition(pos.x, pos.y, 0.0f);
                obj.GetTransform().SetRotation(0.0f, 0.0f, angle);
            }
            for (auto& child : obj.GetChildren()) {
                PostPhysicsUpdateRecursive(*child);
            }
        }
    } // namespace detail

    void Scene::PostPhysicsUpdate() {
        if (!m_Active) return;
        for (auto& obj : m_Objects) {
            detail::PostPhysicsUpdateRecursive(*obj);
        }
    }

    void Scene::CollectRenderCommands(IRenderQueue& queue) {
        if (!m_Active) return;
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

    // ──────────────────────────────────────────────
    // 序列化/反序列化
    // ──────────────────────────────────────────────

    bool Scene::SaveToFile(const std::string& filePath) const {
        return JsonSerializer::SaveToFile(*this, filePath);
    }

    bool Scene::LoadFromFile(const std::string& filePath) {
        return JsonSerializer::LoadFromFile(*this, filePath);
    }

} // namespace Engine
