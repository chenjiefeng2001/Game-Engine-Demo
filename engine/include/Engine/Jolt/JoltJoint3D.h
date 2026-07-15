#pragma once

/**
 * @file JoltJoint3D.h
 * @brief Jolt Physics 3D 约束/关节实现 — v5.0
 *
 * 将 IJoint3D 接口映射到 JPH::TwoBodyConstraint。
 * 支持类型：
 *   - Hinge (铰链/门) → JPH::HingeConstraint
 *   - Ball (球窝/布娃娃) → JPH::PointConstraint
 *   - Slider (滑块/活塞) → JPH::SliderConstraint
 *   - Fixed (固定) → JPH::FixedConstraint
 *   - Distance (距离) → JPH::DistanceConstraint
 *   - SixDOF (六自由度) → JPH::SixDOFConstraint
 */

#include "Engine/Core/Physics/IJoint3D.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/Constraints/Constraint.h>

namespace Engine {

class JoltJoint3D final : public IJoint3D {
public:
    JoltJoint3D(JPH::TwoBodyConstraint* constraint, JointType3D type);
    ~JoltJoint3D() override;

    JointType3D GetType() const override { return m_Type; }

    void SetLimits(const JointLimits3D& limits) override;
    JointLimits3D GetLimits() const override;

    void EnableMotor(bool enable) override;
    bool IsMotorEnabled() const override;
    void SetMotorSpeed(float32 speed) override;
    float32 GetMotorSpeed() const override;
    void SetMaxMotorTorque(float32 torque) override;
    float32 GetMaxMotorTorque() const override;

    float32 GetCurrentAngle() const override;
    float32 GetCurrentDistance() const override;

    IPhysicsBody3D* GetBodyA() const override { return m_BodyA; }
    IPhysicsBody3D* GetBodyB() const override { return m_BodyB; }
    void* GetNativeJoint() override { return m_Constraint; }

    /** 获取原生 Jolt 约束指针 */
    JPH::TwoBodyConstraint* GetConstraint() const { return m_Constraint; }

private:
    JPH::TwoBodyConstraint* m_Constraint = nullptr;
    JointType3D m_Type = JointType3D::Fixed;
    IPhysicsBody3D* m_BodyA = nullptr;
    IPhysicsBody3D* m_BodyB = nullptr;
};

} // namespace Engine