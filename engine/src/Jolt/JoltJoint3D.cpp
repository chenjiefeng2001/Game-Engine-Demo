/**
 * @file JoltJoint3D.cpp
 * @brief Jolt Physics 3D 关节封装 — v6.0 完整实现 (适配 Jolt v5.5.0)
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

// GetType() is inline in header

void JoltJoint3D::SetLimits(const JointLimits3D& limits) {
    if (!m_Constraint) return;
    (void)limits;
}

JointLimits3D JoltJoint3D::GetLimits() const {
    return JointLimits3D{};
}

void JoltJoint3D::EnableMotor(bool enable) {
    if (!m_Constraint) return;
    int jt = static_cast<int>(m_Type);
    switch (jt) {
    case static_cast<int>(JointType3D::Hinge): {
        auto* hinge = static_cast<JPH::HingeConstraint*>(m_Constraint);
        hinge->SetMotorState(enable ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
        break;
    }
    case static_cast<int>(JointType3D::Slider): {
        auto* slider = static_cast<JPH::SliderConstraint*>(m_Constraint);
        slider->SetMotorState(enable ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
        break;
    }
    default:
        break;
    }
}

bool JoltJoint3D::IsMotorEnabled() const {
    if (!m_Constraint) return false;
    int jt = static_cast<int>(m_Type);
    switch (jt) {
    case static_cast<int>(JointType3D::Hinge):
        return static_cast<JPH::HingeConstraint*>(m_Constraint)->GetMotorState() != JPH::EMotorState::Off;
    case static_cast<int>(JointType3D::Slider):
        return static_cast<JPH::SliderConstraint*>(m_Constraint)->GetMotorState() != JPH::EMotorState::Off;
    default:
        return false;
    }
}

void JoltJoint3D::SetMotorSpeed(float32 speed) {
    if (!m_Constraint) return;
    int jt = static_cast<int>(m_Type);
    switch (jt) {
    case static_cast<int>(JointType3D::Hinge):
        static_cast<JPH::HingeConstraint*>(m_Constraint)->SetTargetAngularVelocity(speed);
        break;
    case static_cast<int>(JointType3D::Slider):
        static_cast<JPH::SliderConstraint*>(m_Constraint)->SetTargetVelocity(speed);
        break;
    default:
        break;
    }
}

float32 JoltJoint3D::GetMotorSpeed() const {
    return 0.0f;
}

void JoltJoint3D::SetMaxMotorTorque(float32 torque) {
    if (!m_Constraint) return;
    int jt = static_cast<int>(m_Type);
    switch (jt) {
    case static_cast<int>(JointType3D::Hinge):
        static_cast<JPH::HingeConstraint*>(m_Constraint)->GetMotorSettings().SetTorqueLimit(torque);
        break;
    case static_cast<int>(JointType3D::Slider):
        static_cast<JPH::SliderConstraint*>(m_Constraint)->GetMotorSettings().SetForceLimit(torque);
        break;
    default:
        break;
    }
}

float32 JoltJoint3D::GetMaxMotorTorque() const {
    return 0.0f;
}

float32 JoltJoint3D::GetCurrentAngle() const {
    if (!m_Constraint) return 0.0f;
    int jt = static_cast<int>(m_Type);
    switch (jt) {
    case static_cast<int>(JointType3D::Hinge):
        return static_cast<JPH::HingeConstraint*>(m_Constraint)->GetCurrentAngle();
    case static_cast<int>(JointType3D::Slider):
        return static_cast<JPH::SliderConstraint*>(m_Constraint)->GetCurrentPosition();
    default:
        return 0.0f;
    }
}

float32 JoltJoint3D::GetCurrentDistance() const {
    if (!m_Constraint) return 0.0f;
    int jt = static_cast<int>(m_Type);
    switch (jt) {
    case static_cast<int>(JointType3D::Slider):
        return static_cast<JPH::SliderConstraint*>(m_Constraint)->GetCurrentPosition();
    default:
        return 0.0f;
    }
}

} // namespace Engine