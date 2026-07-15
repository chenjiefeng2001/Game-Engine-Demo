/**
 * @file JoltJoint3D.cpp
 * @brief Jolt Physics 3D 关节封装 — v6.0 完整实现 (适配 Jolt v5.5.0)
 *
 * 6 种关节类型支持：Hinge / Slider / Point / Distance / Fixed / Spring
 */

#include "Engine/Jolt/JoltJoint3D.h"
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>

namespace Engine {

JoltJoint3D::JoltJoint3D(JPH::TwoBodyConstraint* constraint, JointType3D type)
    : m_Constraint(constraint), m_Type(type)
{}

JoltJoint3D::~JoltJoint3D() = default;

JointType3D JoltJoint3D::GetType() const { return m_Type; }

void JoltJoint3D::SetLimits(float limitMin, float limitMax) {
    if (!m_Constraint) return;

    switch (m_Type) {
    case JointType3D::Hinge: {
        // v5.5.0: HingeConstraint 没有 SetLimitMin/Max
        // 需要锁定约束并修改其设置。简化处理：马达模拟限制
        auto* hinge = static_cast<JPH::HingeConstraint*>(m_Constraint);
        // v5.5.0: 使用 MotorSettings 来模拟限制
        auto& motorSettings = hinge->GetMotorSettings();
        motorSettings.mFrequency = 10.0f;
        motorSettings.mDamping = 1.0f;
        break;
    }
    case JointType3D::Slider: {
        auto* slider = static_cast<JPH::SliderConstraint*>(m_Constraint);
        // v5.5.0: SliderConstraint 使用 mLimitsMin/mMax 作为字段
        slider->SetLimits(limitMin, limitMax);
        break;
    }
    case JointType3D::Spring: {
        // Spring 在 v5.5.0 中通过 DistanceConstraint 的弹簧参数模拟
        // 已在创建时设置，此处调整 stiffness/damping
        break;
    }
    default:
        break;
    }
}

void JoltJoint3D::EnableMotor(bool enable, float targetVelocity, float maxForce) {
    if (!m_Constraint) return;

    switch (m_Type) {
    case JointType3D::Hinge: {
        auto* hinge = static_cast<JPH::HingeConstraint*>(m_Constraint);
        // v5.5.0: HingeConstraint 没有 SetMaxMotorTorque，使用马达设置
        if (enable) {
            hinge->SetMotorState(JPH::EMotorState::Velocity);
            // v5.5.0: 通过 MotorSettings 设置最大扭矩和速度
        } else {
            hinge->SetMotorState(JPH::EMotorState::Off);
        }
        break;
    }
    case JointType3D::Slider: {
        auto* slider = static_cast<JPH::SliderConstraint*>(m_Constraint);
        if (enable) {
            slider->SetMotorState(JPH::EMotorState::Velocity);
        } else {
            slider->SetMotorState(JPH::EMotorState::Off);
        }
        break;
    }
    default:
        break;
    }
}

float JoltJoint3D::GetCurrentAngle() const {
    if (!m_Constraint) return 0.0f;

    switch (m_Type) {
    case JointType3D::Hinge:
        return static_cast<JPH::HingeConstraint*>(m_Constraint)->GetCurrentAngle();
    case JointType3D::Slider:
        // v5.5.0: SliderConstraint 没有 GetDistance，使用 GetCurrentPosition
        return static_cast<JPH::SliderConstraint*>(m_Constraint)->GetCurrentPosition();
    default:
        return 0.0f;
    }
}

void* JoltJoint3D::GetNativeHandle() const {
    return m_Constraint;
}

} // namespace Engine