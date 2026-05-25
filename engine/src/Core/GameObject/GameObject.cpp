#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/RHI/IRenderQueue.h"
#include "Engine/Core/Renderer/SpriteBatch.h"
#include <algorithm>
#include <cstring>

namespace Engine {

    // ============================================================
    // EntityManager 全局访问器（由 Scene 在初始化时设置）
    // ============================================================
    // 为 GameObject 句柄提供方便的 EntityManager 访问。
    // 新代码推荐直接通过 Scene::GetEntityManager() 获取。
    static EntityManager* s_ActiveEntityManager = nullptr;

    void Scene::SetActiveEntityManager(EntityManager* em) {
        s_ActiveEntityManager = em;
    }

    EntityManager* Scene::GetActiveEntityManager() {
        return s_ActiveEntityManager;
    }

    // ============================================================
    // GameObject 构造 / 析构
    // ============================================================

    GameObject::GameObject()
        : m_Name("GameObject") {
    }

    GameObject::GameObject(std::string name)
        : m_Name(std::move(name)) {
    }

    GameObject::~GameObject() {
        m_Children.clear();
    }

    // ============================================================
    // 生命周期钩子（子类可重写）
    // ============================================================

    void GameObject::OnCreate() {}
    void GameObject::Update(float32 dt) {
        // 默认递归更新子对象
        for (auto& child : m_Children) {
            child->Update(dt);
        }
    }
    void GameObject::Render() {}
    void GameObject::OnDestroy() {}

    // ============================================================
    // 组件访问（全部委托给 EntityManager）
    // ============================================================

    bool GameObject::IsActiveInHierarchy() const {
        if (!m_Active) return false;
        if (m_Parent)  return m_Parent->IsActiveInHierarchy();
        return true;
    }

    void GameObject::SetParent(GameObject* parent) {
        if (m_Parent == parent) return;

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
    }

    void GameObject::AddChild(std::shared_ptr<GameObject> child) {
        if (!child) return;
        auto it = std::find(m_Children.begin(), m_Children.end(), child);
        if (it != m_Children.end()) return;
        child->SetParent(this);
        m_Children.push_back(std::move(child));
    }

    bool GameObject::RemoveChild(GameObject* child) {
        if (!child) return false;
        auto it = std::find_if(m_Children.begin(), m_Children.end(),
            [child](const std::shared_ptr<GameObject>& c) { return c.get() == child; });
        if (it == m_Children.end()) return false;
        (*it)->SetParent(nullptr);
        m_Children.erase(it);
        return true;
    }

    GameObject* GameObject::FindChild(const std::string& name) {
        for (auto& child : m_Children) {
            if (child->GetName() == name) return child.get();
        }
        return nullptr;
    }

    const GameObject* GameObject::FindChild(const std::string& name) const {
        for (const auto& child : m_Children) {
            if (child->GetName() == name) return child.get();
        }
        return nullptr;
    }

    // ============================================================
    // 组件访问委托
    // ============================================================

    TransformComponent& GameObject::GetTransform() {
        auto* em = s_ActiveEntityManager;
        assert(em && m_Entity != ENTITY_NULL);
        auto* t = em->GetComponent<TransformComponent>(m_Entity);
        if (!t) {
            return em->AddComponent<TransformComponent>(m_Entity);
        }
        return *t;
    }

    const TransformComponent& GameObject::GetTransform() const {
        auto* em = s_ActiveEntityManager;
        assert(em && m_Entity != ENTITY_NULL);
        auto* t = em->GetComponent<TransformComponent>(m_Entity);
        assert(t);
        return *t;
    }

    bool GameObject::HasTransform() const noexcept {
        auto* em = s_ActiveEntityManager;
        return em && m_Entity != ENTITY_NULL && em->HasComponent<TransformComponent>(m_Entity);
    }

    SpriteComponent& GameObject::GetSprite() {
        auto* em = s_ActiveEntityManager;
        assert(em && m_Entity != ENTITY_NULL);
        auto* s = em->GetComponent<SpriteComponent>(m_Entity);
        if (!s) {
            return em->AddComponent<SpriteComponent>(m_Entity);
        }
        return *s;
    }

    const SpriteComponent* GameObject::GetSprite() const {
        auto* em = s_ActiveEntityManager;
        if (!em || m_Entity == ENTITY_NULL) return nullptr;
        return em->GetComponent<SpriteComponent>(m_Entity);
    }

    bool GameObject::HasSprite() const noexcept {
        auto* em = s_ActiveEntityManager;
        if (!em || m_Entity == ENTITY_NULL) return false;
        auto* s = em->GetComponent<SpriteComponent>(m_Entity);
        return s && (s->HasTexture() || s->IsVisible());
    }

    PhysicsComponent& GameObject::GetPhysics() {
        auto* em = s_ActiveEntityManager;
        assert(em && m_Entity != ENTITY_NULL);
        auto* p = em->GetComponent<PhysicsComponent>(m_Entity);
        if (!p) {
            return em->AddComponent<PhysicsComponent>(m_Entity);
        }
        return *p;
    }

    const PhysicsComponent* GameObject::GetPhysics() const {
        auto* em = s_ActiveEntityManager;
        if (!em || m_Entity == ENTITY_NULL) return nullptr;
        return em->GetComponent<PhysicsComponent>(m_Entity);
    }

    bool GameObject::HasPhysics() const noexcept {
        auto* em = s_ActiveEntityManager;
        if (!em || m_Entity == ENTITY_NULL) return false;
        auto* p = em->GetComponent<PhysicsComponent>(m_Entity);
        return p && p->HasBody();
    }

    // ============================================================
    // 渲染
    // ============================================================

    void GameObject::CollectRenderCommands(IRenderQueue& queue) {
        if (!m_Active || m_Entity == ENTITY_NULL) return;
        auto* em = s_ActiveEntityManager;
        if (!em) return;

        auto* sprite = em->GetComponent<SpriteComponent>(m_Entity);
        if (!sprite || !sprite->IsVisible()) return;

        auto* transform = em->GetComponent<TransformComponent>(m_Entity);
        if (!transform) return;

        RenderCommand cmd;
        const Mat4& world = transform->GetWorldMatrix();
        std::memcpy(cmd.worldMatrix, world.Data(), sizeof(cmd.worldMatrix));

        cmd.uv[0] = sprite->GetUVX();
        cmd.uv[1] = sprite->GetUVY();
        cmd.uv[2] = sprite->GetUVW();
        cmd.uv[3] = sprite->GetUVH();

        const auto& color = sprite->GetColor();
        cmd.color[0] = color.x;
        cmd.color[1] = color.y;
        cmd.color[2] = color.z;
        cmd.color[3] = color.w;

        cmd.texture = sprite->GetTexture();
        cmd.sortingLayer = sprite->GetSortingLayer();
        cmd.orderInLayer = sprite->GetOrderInLayer();

        queue.Push(cmd);
    }

    void GameObject::SubmitSprite(ISpriteBatch& batch) {
        if (!m_Active || m_Entity == ENTITY_NULL) return;
        auto* em = s_ActiveEntityManager;
        if (!em) return;

        auto* sprite = em->GetComponent<SpriteComponent>(m_Entity);
        if (!sprite || !sprite->IsVisible()) return;

        auto* transform = em->GetComponent<TransformComponent>(m_Entity);
        if (!transform) return;

        const float32* worldData = transform->GetWorldMatrixData();
        SpriteData data = sprite->ToSpriteData(worldData);
        batch.Draw(data);
    }

} // namespace Engine
