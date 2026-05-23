#pragma once

#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include <string>
#include <vector>
#include <memory>

namespace Engine {

    class ISpriteBatch;
    class Shader;


    class GameObject {
    public:
        GameObject();
        explicit GameObject(std::string name);
        virtual ~GameObject();
        /** 对象创建后调用（用于初始化资源） */
        virtual void OnCreate() {}
        /** 每帧更新（dt 为帧时间，单位秒）—— 默认递归更新所有子对象 */
        virtual void Update(float dt);
        /** 每帧渲染 */
        virtual void Render() {}
        /** 对象销毁前调用（用于清理资源） */
        virtual void OnDestroy() {}

        // ── 变换 ──
        TransformComponent& GetTransform() noexcept { return m_Transform; }
        const TransformComponent& GetTransform() const noexcept { return m_Transform; }

        // ── 精灵 ──
        SpriteComponent& GetSprite() noexcept { return m_Sprite; }
        const SpriteComponent& GetSprite() const noexcept { return m_Sprite; }
        bool HasSprite() const noexcept { return m_Sprite.HasTexture() || m_Sprite.IsVisible(); }

        // ── 名称 ──
        void SetName(const std::string& name) { m_Name = name; }
        const std::string& GetName() const noexcept { return m_Name; }

        // ── 活动状态 ──
        void SetActive(bool active) { m_Active = active; }
        bool IsActive() const noexcept { return m_Active; }
        bool IsActiveInHierarchy() const;

        // ── 父子层级 ──
        void SetParent(GameObject* parent);
        GameObject* GetParent() const noexcept { return m_Parent; }

        void AddChild(std::shared_ptr<GameObject> child);
        bool RemoveChild(GameObject* child);
        GameObject* FindChild(const std::string& name);
        const GameObject* FindChild(const std::string& name) const;
        const std::vector<std::shared_ptr<GameObject>>& GetChildren() const noexcept {
            return m_Children;
        }

        // ── 便捷方法 ──
        /** 提交精灵到批渲染器（如果已添加精灵组件） */
        void SubmitSprite(ISpriteBatch& batch);

    protected:
        std::string m_Name;

        TransformComponent m_Transform;
        SpriteComponent    m_Sprite;

        bool m_Active = true;

        // ── 层级 ──
        GameObject* m_Parent = nullptr;
        std::vector<std::shared_ptr<GameObject>> m_Children;
    };

} // namespace Engine
