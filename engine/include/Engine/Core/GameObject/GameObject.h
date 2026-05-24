#pragma once

#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include "Engine/Core/RHI/IRenderable.h"
#include "Engine/Core/RHI/RenderCommand.h"
#include <string>
#include <vector>
#include <memory>

namespace Engine {

    class ISpriteBatch;
    class Shader;
    class IRenderQueue;


    class GameObject : public IRenderable {
    public:
        GameObject();
        explicit GameObject(std::string name);
        virtual ~GameObject();
        virtual void OnCreate() {}

        virtual void Update(float32 dt);

        virtual void Render() {}

        virtual void OnDestroy() {}

        // ── IRenderable ──
        /**
         * @brief 收集本对象的渲染命令到队列
         *
         * RHI 原则：不直接操作 ISpriteBatch，而是通过 IRenderQueue
         * 提交纯数据 RenderCommand。SceneRenderer 负责消费这些命令。
         */
        void CollectRenderCommands(IRenderQueue& queue) override;


        TransformComponent& GetTransform() noexcept { return m_Transform; }
        const TransformComponent& GetTransform() const noexcept { return m_Transform; }


        SpriteComponent& GetSprite() noexcept { return m_Sprite; }
        const SpriteComponent& GetSprite() const noexcept { return m_Sprite; }
        bool HasSprite() const noexcept { return m_Sprite.HasTexture() || m_Sprite.IsVisible(); }


        void SetName(const std::string& name) { m_Name = name; }
        const std::string& GetName() const noexcept { return m_Name; }

        PhysicsComponent& GetPhysics() noexcept { return m_Physics; }
        const PhysicsComponent& GetPhysics() const noexcept { return m_Physics; }
        bool HasPhysics() const noexcept { return m_Physics.HasBody(); }


        void SetActive(bool active) { m_Active = active; }
        bool IsActive() const noexcept { return m_Active; }
        bool IsActiveInHierarchy() const;

        void SetParent(GameObject* parent);
        GameObject* GetParent() const noexcept { return m_Parent; }

        void AddChild(std::shared_ptr<GameObject> child);
        bool RemoveChild(GameObject* child);
        GameObject* FindChild(const std::string& name);
        const GameObject* FindChild(const std::string& name) const;
        const std::vector<std::shared_ptr<GameObject>>& GetChildren() const noexcept {
            return m_Children;
        }

        void SubmitSprite(ISpriteBatch& batch);

    protected:
        std::string m_Name;

        TransformComponent  m_Transform;
        SpriteComponent     m_Sprite;
        PhysicsComponent    m_Physics;

        bool m_Active = true;

        GameObject* m_Parent = nullptr;
        std::vector<std::shared_ptr<GameObject>> m_Children;
    };

} // namespace Engine
