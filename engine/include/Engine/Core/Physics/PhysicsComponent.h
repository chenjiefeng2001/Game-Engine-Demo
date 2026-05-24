#pragma once

/**
 * @file PhysicsComponent.h
 * @brief 物理组件 — 可挂载到 GameObject 上，使其受物理引擎驱动
 *
 * 设计原则：
 *   - 组件与 GameObject 生命周期解耦
 *   - 自动同步 TransformComponent <-> IPhysicsBody
 *   - 不直接依赖 Box2D 类型，只依赖抽象接口
 *
 * 使用方式：
 * @code
 *   auto obj = std::make_shared<GameObject>("Box");
 *
 *   BodyDef def;
 *   def.type = BodyType::Dynamic;
 *   def.shape.type = ShapeType::Box;
 *   def.shape.boxSize = {0.5f, 0.5f};
 *   obj->GetPhysics().CreateBody(physicsWorld, def);
 *
 *   // 每帧：
 *   physicsWorld->Step(dt);                          // 物理模拟
 *   obj->GetPhysics().SyncPhysicsToTransform(*obj); // Body → Transform
 * @endcode
 */

#include "Engine/Core/Physics/IPhysicsBody.h"
#include "Engine/Core/Physics/IPhysicsWorld.h"
#include "Engine/Types.h"
#include <memory>

namespace Engine {

    class GameObject;

    class PhysicsComponent {
    public:
        PhysicsComponent() = default;
        ~PhysicsComponent();

        PhysicsComponent(const PhysicsComponent&) = delete;
        PhysicsComponent& operator=(const PhysicsComponent&) = delete;

        PhysicsComponent(PhysicsComponent&&) noexcept = default;
        PhysicsComponent& operator=(PhysicsComponent&&) noexcept = default;

        // ── 初始化 / 销毁 ──
        /**
         * @brief 在物理世界中创建刚体
         * @param world 物理世界
         * @param def   刚体定义
         *
         * 注意：PhysicsComponent 会通过 shared_ptr 持有物理世界的引用，
         * 确保在组件销毁前世界不会被提前释放。
         */
        void CreateBody(std::shared_ptr<IPhysicsWorld> world, const BodyDef& def);

/**
     * @brief 销毁刚体（从物理世界中移除）
     *
     * 安全地在任何时刻调用：
     *   - 如果物理世界仍然存活，会从世界中移除刚体。
     *   - 如果物理世界已被销毁，只释放本组件的引用。
     */
        void DestroyBody();

        // ── 双向同步 ──
        /**
         * @brief 将 Transform 的位置/角度写入物理体
         *
         * 用于：
         *   - 物理体创建后初始化位置
         *   - Kinematic 物体由游戏逻辑驱动位置
         *   - Teleport / 复位操作
         */
        void SyncTransformToPhysics(const Vec2& position, float32 angle);

        /**
         * @brief 将物理体的位置/角度读回到 TransformComponent
         * @param outPosition 输出位置
         * @param outAngle    输出角度（弧度）
         */
        void SyncPhysicsToTransform(Vec2& outPosition, float32& outAngle) const;

        // ── 碰撞事件回调（游戏对象级别） ──
        using CollisionEvent = std::function<void(const ContactInfo& info)>;
        using CollisionPersistEvent = std::function<void(const ContactPersistData& data)>;

        /** 设置碰撞进入回调（当本物体与其他物体开始碰撞时触发） */
        void SetOnCollisionEnter(CollisionEvent cb) { m_OnCollisionEnter = std::move(cb); }
        /** 设置碰撞持续回调（碰撞接触持续期间每帧触发） */
        void SetOnCollisionStay(CollisionPersistEvent cb) { m_OnCollisionStay = std::move(cb); }
        /** 设置碰撞离开回调（碰撞结束时触发） */
        void SetOnCollisionExit(CollisionEvent cb) { m_OnCollisionExit = std::move(cb); }

        // 以下由物理系统内部调用，用于路由碰撞事件
        void InvokeCollisionEnter(const ContactInfo& info) const { if (m_OnCollisionEnter) m_OnCollisionEnter(info); }
        void InvokeCollisionStay(const ContactPersistData& data) const { if (m_OnCollisionStay) m_OnCollisionStay(data); }
        void InvokeCollisionExit(const ContactInfo& info) const { if (m_OnCollisionExit) m_OnCollisionExit(info); }

        // ── 便捷操作 ──
        void ApplyForce(const Vec2& force) {
            if (m_Body) m_Body->ApplyForceToCenter(force);
        }

        void ApplyImpulse(const Vec2& impulse) {
            if (m_Body) m_Body->ApplyLinearImpulse(impulse, m_Body->GetPosition());
        }

        void SetLinearVelocity(const Vec2& velocity) {
            if (m_Body) m_Body->SetLinearVelocity(velocity);
        }

        Vec2 GetLinearVelocity() const {
            return m_Body ? m_Body->GetLinearVelocity() : Vec2(0.0f, 0.0f);
        }

        void SetAwake(bool awake) {
            if (m_Body) m_Body->SetAwake(awake);
        }

        bool HasBody() const noexcept { return m_Body != nullptr; }
        IPhysicsBody* GetBody() const noexcept { return m_Body.get(); }

    private:
        std::shared_ptr<IPhysicsBody>  m_Body  = nullptr;
        std::shared_ptr<IPhysicsWorld> m_World = nullptr;

        // 碰撞事件回调
        CollisionEvent         m_OnCollisionEnter;
        CollisionPersistEvent  m_OnCollisionStay;
        CollisionEvent         m_OnCollisionExit;
    };

} // namespace Engine
