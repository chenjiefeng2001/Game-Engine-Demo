#pragma once

/**
 * @file IPhysicsBody3D.h
 * @brief 3D 物理刚体接口 — 不依赖任何第三方物理库
 *
 * Jolt/PhysX/Bullet 各自实现此接口。
 * 所有方法使用 Engine::Vec3 / Mat4，不暴露具体引擎类型。
 */

#include "Engine/Core/Physics/PhysicsDefs3D.h"

namespace Engine {

    class IPhysicsBody3D {
    public:
        virtual ~IPhysicsBody3D() = default;

        // ── 变换 ──
        virtual void  SetPosition(const Vec3& position) = 0;
        virtual Vec3  GetPosition() const = 0;

        /** 设置旋转（欧拉角 pitch, yaw, roll） */
        virtual void  SetRotation(const Vec3& euler) = 0;
        virtual Vec3  GetRotation() const = 0;

        /** 设置旋转（四元数，无万向锁） */
        virtual void  SetRotationQuat(const Quat& q) = 0;

        /** 获取旋转（四元数，无万向锁） */
        virtual Quat  GetRotationQuat() const = 0;

        /** 直接获取 4×4 世界变换矩阵（用于渲染同步） */
        virtual Mat4  GetWorldMatrix() const = 0;

        // ── 运动 ──
        virtual void  SetLinearVelocity(const Vec3& velocity) = 0;
        virtual Vec3  GetLinearVelocity() const = 0;
        virtual void  SetAngularVelocity(const Vec3& omega) = 0;
        virtual Vec3  GetAngularVelocity() const = 0;

        // ── 力 / 冲量 ──
        virtual void ApplyForce(const Vec3& force, const Vec3& point) = 0;
        virtual void ApplyForceAtCenter(const Vec3& force) = 0;
        virtual void ApplyImpulse(const Vec3& impulse, const Vec3& point) = 0;

        // ── 类型 / 质量 ──
        virtual void     SetType(BodyType3D type) = 0;
        virtual BodyType3D GetType() const = 0;

        virtual float32  GetMass() const = 0;
        virtual float32  GetInertia() const = 0;
        virtual Mat4     GetInertiaTensor() const = 0;

        // ── 阻尼 ──
        virtual void  SetLinearDamping(float32 damping) = 0;
        virtual float32 GetLinearDamping() const = 0;
        virtual void  SetAngularDamping(float32 damping) = 0;
        virtual float32 GetAngularDamping() const = 0;

        // ── 运动限制 ──
        virtual void  SetMaxLinearVelocity(float32 maxVel) = 0;
        virtual float32 GetMaxLinearVelocity() const = 0;
        virtual void  SetMaxAngularVelocity(float32 maxVel) = 0;
        virtual float32 GetMaxAngularVelocity() const = 0;

        // ── v5.0: 形状替换（废弃 AddFixture 语义）──
        /**
         * @brief 替换刚体的形状（运行时修改碰撞体拓扑）
         * @param shapeDef 新形状定义
         *
         * Jolt 中改变形状需要构造新 CompoundShape 并通过 SetShape 整体替换。
         * 不再支持 Box2D 风格的 "一个 Body 上 Add/Remove 多个 Fixture"，
         * 改为 SetShape 整体替换，自动重算质心和惯性张量。
         */
        virtual void SetShape(const ShapeDef3D& shapeDef) = 0;

        // ── 碰撞过滤（v5.0: 调用 Jolt BodyInterface::SetObjectLayer）──
        virtual void   SetCollisionFilter(uint16 categoryBits,
                                          uint16 maskBits,
                                          int32  groupIndex) = 0;

        // ── 激活 ──
        virtual void  SetActive(bool active) = 0;
        virtual bool  IsActive() const = 0;

        // ── 休眠 ──
        virtual bool  IsSleeping() const = 0;
        virtual void  SetSleeping(bool sleep) = 0;

        // ── 内部（组件路由用）──
        virtual void  SetUserData(void* data) = 0;
        virtual void* GetUserData() const = 0;
        virtual void  SetComponentRef(void* ref) = 0;
        virtual void* GetComponentRef() const = 0;

        // ── 底层访问（供具体实现使用）──
        virtual void* GetNativeBody() = 0;
    };

} // namespace Engine
