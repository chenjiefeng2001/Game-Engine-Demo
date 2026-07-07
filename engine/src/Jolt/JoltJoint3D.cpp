/**
 * @file JoltJoint3D.cpp
 * @brief Jolt Physics 3D 关节实现 — v6.0
 *
 * 将 IJoint3D 接口映射到 Jolt 的约束系统。
 * 支持 Hinge/Slider/Ball/Fixed/Distance/SixDOF。
 */

#include "Engine/Jolt/JoltJoint3D.h"
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>

namespace Engine {

JoltJoint3D::JoltJoint3D(JPH::TwoBodyConstraint* constraint, const JointDef3D& def)
    : m_Constraint(constraint)
    , m_Type(def.type)
    , m_BodyA(def.bodyA)
    , m_BodyB(def.bodyB)
{
}

JoltJoint3D::~JoltJoint3D() = default;

void JoltJoint3D::SetLimits(const JointLimits3D& limits) {
    if (!m_Constraint) return;

    switch (m_Type) {
        case JointType3D::Hinge: {
            auto* hinge = static_cast<JPH::HingeConstraint*>(m_Constraint);
            if (limits.enableMin || limits.enableMax) {
                hinge->SetLimits(limits.min, limits.max);
            }
            break;
        }
        case JointType3D::Slider: {
            auto* slider = static_cast<JPH::SliderConstraint*>(m_Constraint);
            if (limits.enableMin || limits.enableMax) {
                slider->SetLimits(limits.min, limits.max);
            }
            break;
        }
        case JointType3D::SixDOF: {
            auto* sixdof = static_cast<JPH::SixDOFConstraint*>(m_Constraint);
            for (int i = 0; i < 6; ++i) {
                if (limits.enableMin) sixdof->SetLimitMin(JPH::SixDOFConstraint::EAxis(i), limits.min);
                if (limits.enableMax) sixdof->SetLimitMax(JPH::SixDOFConstraint::EAxis(i), limits.max);
            }
            break;
        }
        default: break;
    }
}

JointLimits3D JoltJoint3D::GetLimits() const {
    JointLimits3D result;
    if (!m_Constraint) return result;

    switch (m_Type) {
        case JointType3D::Hinge: {
            auto* hinge = static_cast<JPH::HingeConstraint*>(m_Constraint);
            result.enableMin = true;
            result.enableMax = true;
            result.min = hinge->GetLimitsMin();
            result.max = hinge->GetLimitsMax();
            break;
        }
        default: break;
    }
    return result;
}

void JoltJoint3D::EnableMotor(bool enable) {
    if (!m_Constraint) return;
    switch (m_Type) {
        case JointType3D::Hinge:
            static_cast<JPH::HingeConstraint*>(m_Constraint)->SetMotorState(
                enable ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
            break;
        case JointType3D::Slider:
            static_cast<JPH::SliderConstraint*>(m_Constraint)->SetMotorState(
                enable ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
            break;
        default: break;
    }
}

bool JoltJoint3D::IsMotorEnabled() const {
    if (!m_Constraint) return false;
    switch (m_Type) {
        case JointType3D::Hinge:
            return static_cast<const JPH::HingeConstraint*>(m_Constraint)->GetMotorState() != JPH::EMotorState::Off;
        case JointType3D::Slider:
            return static_cast<const JPH::SliderConstraint*>(m_Constraint)->GetMotorState() != JPH::EMotorState::Off;
        default: return false;
    }
}

void JoltJoint3D::SetMotorSpeed(float32 speed) {
    if (!m_Constraint) return;
    switch (m_Type) {
        case JointType3D::Hinge:
            static_cast<JPH::HingeConstraint*>(m_Constraint)->SetTargetAngularVelocity(speed);
            break;
        case JointType3D::Slider:
            static_cast<JPH::SliderConstraint*>(m_Constraint)->SetTargetVelocity(speed);
            break;
        default: break;
    }
}

float32 JoltJoint3D::GetMotorSpeed() const {
    if (!m_Constraint) return 0.0f;
    switch (m_Type) {
        case JointType3D::Hinge:
            return static_cast<const JPH::HingeConstraint*>(m_Constraint)->GetTargetAngularVelocity();
        case JointType3D::Slider:
            return static_cast<const JPH::SliderConstraint*>(m_Constraint)->GetTargetVelocity();
        default: return 0.0f;
    }
}

void JoltJoint3D::SetMaxMotorTorque(float32 torque) {
    if (!m_Constraint) return;
    switch (m_Type) {
        case JointType3D::Hinge:
            static_cast<JPH::HingeConstraint*>(m_Constraint)->SetMaxMotorTorque(torque);
            break;
        case JointType3D::Slider:
            static_cast<JPH::SliderConstraint*>(m_Constraint)->SetMaxMotorForce(torque);
            break;
        default: break;
    }
}

float32 JoltJoint3D::GetMaxMotorTorque() const {
    if (!m_Constraint) return 0.0f;
    switch (m_Type) {
        case JointType3D::Hinge:
            return static_cast<const JPH::HingeConstraint*>(m_Constraint)->GetMaxMotorTorque();
        case JointType3D::Slider:
            return 0.0f;
        default: return 0.0f;
    }
}

float32 JoltJoint3D::GetCurrentAngle() const {
    if (!m_Constraint || m_Type != JointType3D::Hinge) return 0.0f;
    return static_cast<const JPH::HingeConstraint*>(m_Constraint)->GetCurrentAngle();
}

float32 JoltJoint3D::GetCurrentDistance() const {
    if (!m_Constraint) return 0.0f;
    switch (m_Type) {
        case JointType3D::Distance:
            return static_cast<const JPH::DistanceConstraint*>(m_Constraint)->GetDistance();
        case JointType3D::Slider:
            return static_cast<const JPH::SliderConstraint*>(m_Constraint)->GetCurrentPosition();
        default: return 0.0f;
    }
}

} // namespace Engine