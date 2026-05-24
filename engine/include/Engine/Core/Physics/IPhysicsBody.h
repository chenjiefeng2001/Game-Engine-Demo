#pragma once

/**
 * @file IPhysicsBody.h
 * @brief 物理刚体接口 — 不依赖任何第三方物理库
 *
 * 设计原则（与 IRenderQueue 等 RHI 接口一致）：
 *   - 纯虚接口，引擎核心只依赖此抽象
 *   - Box2D 实现层提供具体实现
 *   - 所有方法使用 Engine::Vec2，不暴露 Box2D 类型
 */

#include "Engine/Core/Physics/PhysicsDefs.h"

namespace Engine {

    class IPhysicsBody {
    public:
        virtual ~IPhysicsBody() = default;

        virtual void  SetTransform(const Vec2& position, float32 angle) = 0;
        virtual Vec2  GetPosition() const = 0;
        virtual float32 GetAngle() const = 0;

        virtual void  SetLinearVelocity(const Vec2& velocity) = 0;
        virtual Vec2  GetLinearVelocity() const = 0;
        virtual void  SetAngularVelocity(float32 omega) = 0;
        virtual float32 GetAngularVelocity() const = 0;

        virtual void ApplyForce(const Vec2& force, const Vec2& point) = 0;
        virtual void ApplyForceToCenter(const Vec2& force) = 0;
        virtual void ApplyLinearImpulse(const Vec2& impulse, const Vec2& point) = 0;

        virtual void     SetType(BodyType type) = 0;
        virtual BodyType GetType() const = 0;
        virtual float32  GetMass() const = 0;
        virtual float32  GetInertia() const = 0;

        virtual void  SetLinearDamping(float32 damping) = 0;
        virtual float32 GetLinearDamping() const = 0;
        virtual void  SetAngularDamping(float32 damping) = 0;
        virtual float32 GetAngularDamping() const = 0;

        // ── Fixture 管理（运行时添加/移除形状） ──
        virtual void* AddFixture(const FixtureDef& def) = 0;
        virtual void  RemoveFixture(void* fixtureId) = 0;
        virtual void  ClearFixtures() = 0;

        // ── 内部组件引用（PhysicsComponent 在创建时自动设置，用于碰撞事件路由） ──
        virtual void  SetComponentRef(void* ref) = 0;
        virtual void* GetComponentRef() const = 0;

        virtual void SetFilterData(uint16 categoryBits, uint16 maskBits) = 0;
        virtual void SetGroupIndex(int32 groupIndex) = 0;

        virtual void  SetActive(bool active) = 0;
        virtual bool  IsActive() const = 0;


        virtual void* GetUserData() const = 0;
        virtual void  SetUserData(void* data) = 0;

        virtual void SetAwake(bool awake) = 0;
        virtual bool IsAwake() const = 0;

        virtual void* GetNativeBody() = 0;
    };

} // namespace Engine
