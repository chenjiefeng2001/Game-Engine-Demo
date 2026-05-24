#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/RHI/IRenderQueue.h"
#include <algorithm>
#include <functional>

namespace Engine {

    // ──────────────────────────────────────────────
    // 构造 / 析构
    // ──────────────────────────────────────────────

    Scene::~Scene() {
        OnDestroy();
        m_Objects.clear();
    }

    // ──────────────────────────────────────────────
    // 对象管理
    // ──────────────────────────────────────────────

    void Scene::AddObject(std::shared_ptr<GameObject> obj) {
        if (!obj) return;

        // 避免重复添加同一指针
        auto it = std::find(m_Objects.begin(), m_Objects.end(), obj);
        if (it != m_Objects.end()) return;

        m_Objects.push_back(std::move(obj));
    }

    bool Scene::RemoveObject(GameObject* obj) {
        if (!obj) return false;

        auto it = std::find_if(m_Objects.begin(), m_Objects.end(),
            [obj](const std::shared_ptr<GameObject>& o) {
                return o.get() == obj;
            });

        if (it == m_Objects.end()) return false;

        m_Objects.erase(it);
        return true;
    }

    void Scene::Clear() {
        OnDestroy();
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
    // 生命周期
    // ──────────────────────────────────────────────

    void Scene::OnCreate() {
        for (auto& obj : m_Objects) {
            obj->OnCreate();
            // 递归调用子对象的 OnCreate
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
        for (auto& obj : m_Objects) {
            obj->Update(dt);
        }
    }

    void Scene::Render() {
        for (auto& obj : m_Objects) {
            obj->Render();
            // GameObject 基类的 Render 为空，但子类可能重写
        }
    }

    void Scene::OnDestroy() {
        for (auto& obj : m_Objects) {
            obj->OnDestroy();
            // 递归调用子对象的 OnDestroy
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
            // 如果有物理组件，同步位置/角度回 Transform
            if (obj.HasPhysics()) {
                Vec2 pos;
                float32 angle;
                obj.GetPhysics().SyncPhysicsToTransform(pos, angle);

                // 仅对 Dynamic 类型的物体做同步（Static/Kinematic 由用户控制）
                // 但为了简单，这里同步所有类型，由用户决定是否创建 PhysicsComponent
                obj.GetTransform().SetPosition(pos.x, pos.y, 0.0f);
                obj.GetTransform().SetRotation(0.0f, 0.0f, angle);
            }

            for (auto& child : obj.GetChildren()) {
                PostPhysicsUpdateRecursive(*child);
            }
        }
    } // namespace detail

    void Scene::PostPhysicsUpdate() {
        for (auto& obj : m_Objects) {
            detail::PostPhysicsUpdateRecursive(*obj);
        }
    }


    void Scene::CollectRenderCommands(IRenderQueue& queue) {
        // 递归收集所有对象的渲染命令
        std::function<void(GameObject&)> collectRecursive =
            [&](GameObject& obj) {
                // 通过 IRenderable 接口收集（GameObject 实现了该接口）
                obj.CollectRenderCommands(queue);

                for (auto& child : obj.GetChildren()) {
                    collectRecursive(*child);
                }
            };

        for (auto& obj : m_Objects) {
            collectRecursive(*obj);
        }
    }

} // namespace Engine
