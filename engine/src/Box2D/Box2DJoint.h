#pragma once

/**
 * @file Box2DJoint.h
 * @brief Box2D 关节实现（Box2D 3.x）
 *
 * 私有头文件，仅在 Box2D 实现层使用。
 * 实现 IJoint 接口，封装 b2JointId。
 */

#include "Engine/Core/Physics/IJoint.h"
#include <box2d/box2d.h>

namespace Engine {

    class Box2DJoint : public IJoint {
    public:
        Box2DJoint(b2JointId jointId, const JointDef& def);
        ~Box2DJoint() override;

        /// 更新鼠标关节的目标位置（每帧调用）
        void UpdateMouseTarget(const Vec2& target);

        // ── IJoint ──
        JointType      GetType() const override;
        IPhysicsBody*  GetBodyA() const override;
        IPhysicsBody*  GetBodyB() const override;
        Vec2           GetAnchorA() const override;
        Vec2           GetAnchorB() const override;
        bool           IsEnabled() const override;
        bool           GetCollideConnected() const override;
        void*          GetUserData() const override;
        void           SetUserData(void* data) override;

        // 特定关节类型
        float32 GetJointAngle() const override;
        float32 GetJointSpeed() const override;
        float32 GetJointTranslation() const override;
        float32 GetCurrentLength() const override;

        // 电机
        void    SetMotorSpeed(float32 speed) override;
        float32 GetMotorSpeed() const override;
        void    SetMaxMotorTorque(float32 torque) override;
        float32 GetMotorTorque(float32 invDt) const override;

        // 限位
        void EnableLimit(bool enable) override;
        void SetLimits(float32 lower, float32 upper) override;

        void* GetNativeJoint() override;

        // ── 内部 ──
        b2JointId GetBox2DJointId() const { return m_JointId; }

    private:
        b2JointId m_JointId = {};
        JointDef  m_Def;  // 缓存创建时的定义
        void*     m_UserData = nullptr;

        // MouseJoint 特有方法
        void SetTarget(const Vec2& target) override;
        Vec2 GetTarget() const override;
        void SetMaxForce(float32 force) override;
        float32 GetMaxForce() const override;
        void SetStiffness(float32 stiffness) override;
        float32 GetStiffness() const override;
        void SetDamping(float32 damping) override;
        float32 GetDamping() const override;
    };

} // namespace Engine
