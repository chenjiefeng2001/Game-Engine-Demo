#pragma once

/**
 * @file PhysicsComponent3D.h
 * @brief 3D 物理组件 — 挂载到 GameObject 上，使其受 3D 物理引擎驱动
 *
 * 设计原则与 2D PhysicsComponent 一致：
 *   - 组件与 GameObject 生命周期解耦
 *   - 自动同步 TransformComponent <-> IPhysicsBody3D
 *   - 不直接依赖 Jolt/PhysX/Bullet 类型
 *
 * 使用方式：
 * @code
 *   auto obj = std::make_shared<GameObject>("Sphere");
 *
 *   BodyDef3D def;
 *   def.type = BodyType3D::Dynamic;
 *   def.shape.type = ShapeType3D::Sphere;
 *   def.shape.sphereRadius = 0.5f;
 *   obj->AddComponent<PhysicsComponent3D>()->CreateBody(physicsWorld, def);
 *
 *   // 每帧：
 *   physicsWorld->Step(dt);
 *   obj->GetComponent<PhysicsComponent3D>()->SyncTransform();
 * @endcode
 */

#include "Engine/Core/Physics/IPhysicsWorld3D.h"
#include "Engine/Core/Physics/IPhysicsBody3D.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Types.h"
#include <memory>

namespace Engine {

    class GameObject;

    class PhysicsComponent3D : public Component {
    public:
        PhysicsComponent3D() = default;
        ~PhysicsComponent3D();

        PhysicsComponent3D(const PhysicsComponent3D&) = delete;
        PhysicsComponent3D& operator=(const PhysicsComponent3D&) = delete;

        // ── 初始化 / 销毁 ──
        /**
         * @brief 在 3D 物理世界中创建刚体
         * @param world 物理世界（shared_ptr 延长生命周期）
         * @param def   刚体定义
         */
        void CreateBody(std::shared_ptr<IPhysicsWorld3D> world,
                        const BodyDef3D& def);

        /**
         * @brief 销毁刚体（从物理世界中移除）
         */
        void DestroyBody();

        // ── 同步 ──
        /**
         * @brief 物理 → 变换 同步
         * 将物理刚体的位置/旋转写回 TransformComponent
         */
        void SyncPhysicsToTransform();

        /**
         * @brief 变换 → 物理 同步
         * 将 TransformComponent 的位置/旋转写回物理刚体
         */
        void SyncTransformToPhysics();

        // ── 查询 ──
        IPhysicsBody3D* GetBody() const { return m_Body.get(); }
        bool HasBody() const { return m_Body != nullptr; }
        std::shared_ptr<IPhysicsWorld3D> GetWorld() const { return m_World.lock(); }

    private:
        // 组件的生命周期回调（由 Component 基类提供）
        void OnDestroy() override;

        std::shared_ptr<IPhysicsBody3D> m_Body;
        std::weak_ptr<IPhysicsWorld3D>  m_World;
    };

} // namespace Engine
