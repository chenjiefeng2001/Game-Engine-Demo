#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include "Engine/Core/RHI/IRenderQueue.h"
#include "Engine/Core/Renderer/SpriteBatch.h"
#include <algorithm>
#include <cstring>

namespace Engine {


    GameObject::GameObject()
        : m_ID(GetNextID()), m_Name("GameObject") {
    }

    GameObject::GameObject(std::string name)
        : m_ID(GetNextID()), m_Name(std::move(name)) {
    }

    GameObject::~GameObject() {
        // 通知所有组件销毁
        for (auto& [hash, comp] : m_Components) {
            (void)hash;
            comp->OnDestroy();
        }
        m_Components.clear();
        // 递归销毁子对象
        m_Children.clear();
    }

    void GameObject::OnCreate() {
        for (auto& [hash, comp] : m_Components) {
            (void)hash;
            if (comp->IsEnabled())
                comp->OnCreate();
        }
    }

    void GameObject::OnDestroy() {
        for (auto& [hash, comp] : m_Components) {
            (void)hash;
            if (comp->IsEnabled())
                comp->OnDestroy();
        }
    }

    bool GameObject::IsActiveInHierarchy() const {
        if (!m_Active) return false;
        if (m_Parent)  return m_Parent->IsActiveInHierarchy();
        return true;
    }

    void GameObject::SetParent(GameObject* parent) {
        if (m_Parent == parent) return;

        // 从旧父级移除
        if (m_Parent) {
            auto& siblings = m_Parent->m_Children;
            auto it = std::remove_if(siblings.begin(), siblings.end(),
                [this](const std::shared_ptr<GameObject>& c) {
                    return c.get() == this;
                });
            if (it != siblings.end())
                siblings.erase(it, siblings.end());
        }

        m_Parent = parent;

        if (m_Parent) {
            // 设置变换层级
            m_Transform.SetParent(&m_Parent->m_Transform);
        } else {
            m_Transform.SetParent(nullptr);
        }
    }

    void GameObject::AddChild(std::shared_ptr<GameObject> child) {
        if (!child) return;

        // 如果已经存在，先移除旧引用
        auto it = std::find(m_Children.begin(), m_Children.end(), child);
        if (it != m_Children.end()) return;

        child->SetParent(this);
        m_Children.push_back(std::move(child));
    }

    bool GameObject::RemoveChild(GameObject* child) {
        if (!child) return false;

        auto it = std::find_if(m_Children.begin(), m_Children.end(),
            [child](const std::shared_ptr<GameObject>& c) {
                return c.get() == child;
            });

        if (it == m_Children.end()) return false;

        (*it)->SetParent(nullptr);
        m_Children.erase(it);
        return true;
    }

    GameObject* GameObject::FindChild(const std::string& name) {
        for (auto& child : m_Children) {
            if (child->GetName() == name)
                return child.get();
        }
        return nullptr;
    }

    const GameObject* GameObject::FindChild(const std::string& name) const {
        for (const auto& child : m_Children) {
            if (child->GetName() == name)
                return child.get();
        }
        return nullptr;
    }

    void GameObject::Update(float32 dt) {
        if (!m_Active) return;

        // 更新所有已启用的组件
        for (auto& [hash, comp] : m_Components) {
            (void)hash;
            if (comp->IsEnabled())
                comp->OnUpdate(dt);
        }

        // 递归更新所有子对象
        for (auto& child : m_Children) {
            child->Update(dt);
        }
    }

    void GameObject::CollectRenderCommands(IRenderQueue& queue) {
        if (!m_Active) return;

        // 收集所有组件的渲染命令
        for (auto& [hash, comp] : m_Components) {
            (void)hash;
            if (comp->IsEnabled())
                comp->CollectRenderCommands(queue);
        }
    }

    void GameObject::SubmitSprite(ISpriteBatch& batch) {
        if (!m_Active) return;

        // 遍历组件，让每个 SpriteComponent 提交
        for (auto& [hash, comp] : m_Components) {
            (void)hash;
            if (!comp->IsEnabled()) continue;

            // dynamic_cast 检查是否是 SpriteComponent
            auto* sprite = dynamic_cast<SpriteComponent*>(comp.get());
            if (sprite && sprite->IsVisible()) {
                const float32* worldData = m_Transform.GetWorldMatrixData();
                SpriteData data = sprite->ToSpriteData(worldData);
                batch.Draw(data);
            }
        }
    }

    // ============================================================
    // 便捷方法：SpriteComponent
    // ============================================================

    SpriteComponent& GameObject::GetSprite() {
        auto* existing = GetComponent<SpriteComponent>();
        if (existing) return *existing;
        return *AddComponent<SpriteComponent>();
    }

    const SpriteComponent* GameObject::GetSprite() const {
        return GetComponent<SpriteComponent>();
    }

    bool GameObject::HasSprite() const noexcept {
        auto* sprite = GetComponent<SpriteComponent>();
        return sprite != nullptr && (sprite->HasTexture() || sprite->IsVisible());
    }

    // ============================================================
    // 便捷方法：PhysicsComponent
    // ============================================================

    PhysicsComponent& GameObject::GetPhysics() {
        auto* existing = GetComponent<PhysicsComponent>();
        if (existing) return *existing;
        return *AddComponent<PhysicsComponent>();
    }

    const PhysicsComponent* GameObject::GetPhysics() const {
        return GetComponent<PhysicsComponent>();
    }

    bool GameObject::HasPhysics() const noexcept {
        auto* physics = GetComponent<PhysicsComponent>();
        return physics != nullptr && physics->HasBody();
    }

} // namespace Engine
