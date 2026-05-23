#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/Renderer/SpriteBatch.h"
#include <algorithm>
#include <stack>

namespace Engine {


    GameObject::GameObject()
        : m_Name("GameObject") {
    }

    GameObject::GameObject(std::string name)
        : m_Name(std::move(name)) {
    }

    GameObject::~GameObject() {
        // 递归销毁子对象
        m_Children.clear();
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

    void GameObject::Update(float dt) {
        // 默认递归更新所有子对象
        // 子类重写 Update 时应调用 GameObject::Update(dt) 来确保子对象得到更新
        for (auto& child : m_Children) {
            child->Update(dt);
        }
    }

    void GameObject::SubmitSprite(ISpriteBatch& batch) {
        if (!m_Active) return;
        if (!m_Sprite.IsVisible()) return;

        // 获取世界矩阵
        const glm::mat4& world = m_Transform.GetWorldMatrix();

        // 将 SpriteComponent 转换为 SpriteData 并提交
        SpriteData data = m_Sprite.ToSpriteData(world);
        batch.Draw(data);
    }

} // namespace Engine
