#pragma once

/**
 * @file JoltPhysicsBody.h
 * @brief Jolt Physics 刚体实现
 */

#include "Engine/Core/Physics/IPhysicsBody3D.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>

namespace Engine {

class JoltPhysicsWorld;

class JoltPhysicsBody final : public IPhysicsBody3D {
public:
    JoltPhysicsBody(JPH::Body* body, JoltPhysicsWorld* world);
    ~JoltPhysicsBody() override;

    // ── 变换 ──
    void SetPosition(const Vec3& position) override;
    Vec3 GetPosition() const override;
    void SetRotation(const Vec3& euler) override;
    Vec3 GetRotation() const override;
    Mat4 GetWorldMatrix() const override;

    // ── 运动 ──
    void SetLinearVelocity(const Vec3& velocity) override;
    Vec3 GetLinearVelocity() const override;
    void SetAngularVelocity(const Vec3& omega) override;
    Vec3 GetAngularVelocity() const override;

    // ── 力 / 冲量 ──
    void ApplyForce(const Vec3& force, const Vec3& point) override;
    void ApplyForceAtCenter(const Vec3& force) override;
    void ApplyImpulse(const Vec3& impulse, const Vec3& point) override;

    // ── 类型 / 质量 ──
    void SetType(BodyType3D type) override;
    BodyType3D GetType() const override;
    float32 GetMass() const override;
    float32 GetInertia() const override;
    Mat4 GetInertiaTensor() const override;

    // ── 阻尼 ──
    void SetLinearDamping(float32 damping) override;
    float32 GetLinearDamping() const override;
    void SetAngularDamping(float32 damping) override;
    float32 GetAngularDamping() const override;

    // ── 运动限制 ──
    void SetMaxLinearVelocity(float32 maxVel) override;
    float32 GetMaxLinearVelocity() const override;
    void SetMaxAngularVelocity(float32 maxVel) override;
    float32 GetMaxAngularVelocity() const override;

    // ── Fixture ──
    void* AddFixture(const FixtureDef3D& def) override;
    void RemoveFixture(void* fixtureId) override;
    void ClearFixtures() override;

    // ── 碰撞过滤 ──
    void SetCollisionFilter(uint16 categoryBits, uint16 maskBits, int32 groupIndex) override;

    // ── 激活 ──
    void SetActive(bool active) override;
    bool IsActive() const override;

    // ── 休眠 ──
    bool IsSleeping() const override;
    void SetSleeping(bool sleep) override;

    // ── 内部 ──
    void SetUserData(void* data) override;
    void* GetUserData() const override;
    void SetComponentRef(void* ref) override;
    void* GetComponentRef() const override;

    void* GetNativeBody() override;

    // ── Jolt 访问 ──
    JPH::BodyID GetBodyID() const { return m_BodyID; }
    JPH::BodyInterface& GetBodyInterface() const;

private:
    JPH::BodyID   m_BodyID;
    JoltPhysicsWorld* m_World;
    void* m_ComponentRef = nullptr;
};

} // namespace Engine