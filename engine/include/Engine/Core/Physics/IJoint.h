#pragma once

/**
 * @file IJoint.h
 * @brief 物理关节接口 — 不依赖任何第三方物理库
 *
 * 设计原则（与 IPhysicsBody 一致）：
 *   - 纯虚接口，引擎核心只依赖此抽象
 *   - Box2D 实现层提供具体实现
 *   - 特定关节类型的方法通过 GetNativeJoint() 在实现层访问
 */

#include "Engine/Core/Physics/PhysicsDefs.h"

namespace Engine {

    class IPhysicsBody;

    class IJoint {
    public:
        virtual ~IJoint() = default;

        /// 关节类型
        virtual JointType GetType() const = 0;

        /// 关联的两个刚体
        virtual IPhysicsBody* GetBodyA() const = 0;
        virtual IPhysicsBody* GetBodyB() const = 0;

        /// 锚点（世界坐标）
        virtual Vec2 GetAnchorA() const = 0;
        virtual Vec2 GetAnchorB() const = 0;

        /// 查询是否已启用（Box2D 2.4.1 不支持运行时禁用关节）
        virtual bool IsEnabled() const = 0;

        /// 碰撞连接
        virtual bool GetCollideConnected() const = 0;

        /// 用户数据
        virtual void* GetUserData() const = 0;
        virtual void  SetUserData(void* data) = 0;

        // ── 特定关节类型的便捷访问 ──

        /// Revolute: 当前关节角度（弧度）
        virtual float32 GetJointAngle() const { return 0.0f; }
        /// Revolute: 角速度
        virtual float32 GetJointSpeed() const { return 0.0f; }

        /// Prismatic: 当前平移量
        virtual float32 GetJointTranslation() const { return 0.0f; }
        /// Prismatic: 平移速度
        virtual float32 GetJointSpeed2() const { return 0.0f; }

        /// Distance: 当前长度
        virtual float32 GetCurrentLength() const { return 0.0f; }

        // ── 电机控制（Revolute / Prismatic / Wheel） ──
        virtual void SetMotorSpeed(float32 speed) { (void)speed; }
        virtual float32 GetMotorSpeed() const { return 0.0f; }
        virtual void SetMaxMotorTorque(float32 torque) { (void)torque; }
        virtual float32 GetMotorTorque(float32 invDt) const { (void)invDt; return 0.0f; }

        // ── 限位控制（Revolute / Prismatic） ──
        virtual void EnableLimit(bool enable) { (void)enable; }
        virtual void SetLimits(float32 lower, float32 upper) { (void)lower; (void)upper; }

        // ── MouseJoint 特有方法 ──
        virtual void SetTarget(const Vec2& target) { (void)target; }
        virtual Vec2 GetTarget() const { return Vec2(0.0f, 0.0f); }
        virtual void SetMaxForce(float32 force) { (void)force; }
        virtual float32 GetMaxForce() const { return 0.0f; }
        virtual void SetStiffness(float32 stiffness) { (void)stiffness; }
        virtual float32 GetStiffness() const { return 0.0f; }
        virtual void SetDamping(float32 damping) { (void)damping; }
        virtual float32 GetDamping() const { return 0.0f; }

        /// 获取原生 Box2D 关节指针（仅实现层使用）
        virtual void* GetNativeJoint() = 0;
    };

} // namespace Engine
