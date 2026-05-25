#pragma once

/**
 * @file GameObject.h
 * @brief 游戏对象 — ECS Entity 的轻量句柄（兼容层）
 *
 * 在 ECS 架构下，GameObject 是一个围绕 Entity ID 的轻量包装，
 * 提供 OOP 风格的便捷接口，底层操作由 EntityManager 执行。
 *
 * 架构关系：
 *   Scene
 *    └── EntityManager（ECS 核心）
 *          ├── Entity (uint32)
 *          ├── TransformComponent (内置)
 *          ├── SpriteComponent (按需)
 *          ├── PhysicsComponent (按需)
 *          └── ... 其他组件
 *
 * 新代码推荐直接使用 ECS API：
 * @code
 *   Entity e = scene.GetEntityManager().Create();
 *   scene.GetEntityManager().AddComponent<SpriteComponent>(e, texture);
 * @endcode
 */

#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/GameObject/SpriteComponent.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include "Engine/Core/ECS/ECS.h"
#include "Engine/Core/RHI/IRenderable.h"
#include "Engine/Core/RHI/RenderCommand.h"
#include <string>
#include <vector>
#include <memory>

namespace Engine {

    class ISpriteBatch;
    class Shader;
    class IRenderQueue;

    /**
     * @brief 游戏对象句柄 — 封装 Entity ID + EntityManager 引用
     *
     * 此类保持与现有沙箱代码的向后兼容，所有 GetSprite/GetPhysics
     * 操作委托给 EntityManager 的组件池。
     * 新实现建议直接通过 EntityManager API 操作。
     */
    class GameObject : public IRenderable {
    public:
        GameObject();
        explicit GameObject(std::string name);
        virtual ~GameObject();

        GameObject(const GameObject&) = delete;
        GameObject& operator=(const GameObject&) = delete;

        // ── 生命周期钩子（子类可重写，默认操作组件） ──
        /** 对象添加到场景后调用 */
        virtual void OnCreate();
        /** 每帧更新 */
        virtual void Update(float32 dt);
        /** 每帧渲染 */
        virtual void Render();
        /** 对象销毁时调用 */
        virtual void OnDestroy();

        // ── ECS Entity ──
        /** 获取底层 ECS Entity ID */
        Entity GetEntity() const noexcept { return m_Entity; }
        /** 设置底层 ECS Entity（由 Scene 在添加到 EntityManager 时调用） */
        void SetEntity(Entity entity) noexcept { m_Entity = entity; }

        // ── IRenderable ──
        void CollectRenderCommands(IRenderQueue& queue) override;

        // ── 组件访问（全部委托给 EntityManager） ──

        /** 变换组件（每个 GameObject 始终拥有） */
        TransformComponent& GetTransform();
        const TransformComponent& GetTransform() const;
        bool HasTransform() const noexcept;

        /** 精灵组件（按需通过 AddComponent 获取） */
        SpriteComponent& GetSprite();
        const SpriteComponent* GetSprite() const;
        bool HasSprite() const noexcept;

        /** 物理组件 */
        PhysicsComponent& GetPhysics();
        const PhysicsComponent* GetPhysics() const;
        bool HasPhysics() const noexcept;

        // ── 通用属性 ──
        void SetName(const std::string& name) { m_Name = name; }
        const std::string& GetName() const noexcept { return m_Name; }

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

        // ECS 实体 ID（由 Scene 管理）
        Entity m_Entity = ENTITY_NULL;

        bool m_Active = true;

        GameObject* m_Parent = nullptr;
        std::vector<std::shared_ptr<GameObject>> m_Children;
    };

} // namespace Engine
