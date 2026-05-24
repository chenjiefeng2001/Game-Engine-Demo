#pragma once

/**
 * @file Box2DPhysicsBody.h
 * @brief Box2D 物理刚体实现
 *
 * 私有头文件，仅在 Box2D 实现层使用。
 * 实现 IPhysicsBody 接口，封装 b2Body。
 */

#include "Engine/Core/Physics/IPhysicsBody.h"
#include <box2d/box2d.h>

namespace Engine {

    class Box2DPhysicsWorld;

    class Box2DPhysicsBody : public IPhysicsBody {
    public:
        Box2DPhysicsBody(b2Body* body, const BodyDef& def);
        ~Box2DPhysicsBody() override;

        // ── IPhysicsBody ──

        // 变换
        void  SetTransform(const Vec2& position, float32 angle) override;
        Vec2  GetPosition() const override;
        float32 GetAngle() const override;

        // 速度
        void  SetLinearVelocity(const Vec2& velocity) override;
        Vec2  GetLinearVelocity() const override;
        void  SetAngularVelocity(float32 omega) override;
        float32 GetAngularVelocity() const override;

        // 力 / 冲量
        void ApplyForce(const Vec2& force, const Vec2& point) override;
        void ApplyForceToCenter(const Vec2& force) override;
        void ApplyLinearImpulse(const Vec2& impulse, const Vec2& point) override;

        // 属性
        void     SetType(BodyType type) override;
        BodyType GetType() const override;
        float32  GetMass() const override;
        float32  GetInertia() const override;

        // ── 阻尼 ──
        void   SetLinearDamping(float32 damping) override;
        float32 GetLinearDamping() const override;
        void   SetAngularDamping(float32 damping) override;
        float32 GetAngularDamping() const override;

        // 碰撞滤波
        void SetFilterData(uint16 categoryBits, uint16 maskBits) override;
        void SetGroupIndex(int32 groupIndex) override;

        // 激活
        void  SetActive(bool active) override;
        bool  IsActive() const override;

        // 用户数据
        void* GetUserData() const override;
        void  SetUserData(void* data) override;

        // 休眠
        void SetAwake(bool awake) override;
        bool IsAwake() const override;

        // 原生指针
        void* GetNativeBody() override;

        // ── 内部 ──
        b2Body* GetBox2DBody() const { return m_Body; }

    private:
        b2Body* m_Body = nullptr;

        // 缓存 BodyDef 中的用户数据
        void* m_UserData = nullptr;
    };

} // namespace Engine
