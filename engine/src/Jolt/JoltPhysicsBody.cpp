/**
 * @file JoltPhysicsBody.cpp
 * @brief Jolt Physics 刚体实现
 */

#include "Engine/Jolt/JoltPhysicsBody.h"
#include "Engine/Jolt/JoltPhysicsWorld.h"
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace Engine {

JoltPhysicsBody::JoltPhysicsBody(JPH::Body* body, JoltPhysicsWorld* world)
    : m_World(world)
{
    m_BodyID = body->GetID();
}

JoltPhysicsBody::~JoltPhysicsBody() = default;

JPH::BodyInterface& JoltPhysicsBody::GetBodyInterface() const {
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    return sys->GetBodyInterface();
}

void JoltPhysicsBody::SetPosition(const Vec3& position) {
    GetBodyInterface().SetPosition(m_BodyID,
        JPH::Vec3(position.x, position.y, position.z),
        JPH::EActivation::Activate);
}

Vec3 JoltPhysicsBody::GetPosition() const {
    JPH::Vec3 pos = GetBodyInterface().GetPosition(m_BodyID);
    return Vec3(pos.GetX(), pos.GetY(), pos.GetZ());
}

void JoltPhysicsBody::SetRotation(const Vec3& euler) {
    glm::quat q = glm::quat(glm::radians(glm::vec3(euler.x, euler.y, euler.z)));
    GetBodyInterface().SetRotation(m_BodyID,
        JPH::Quat(q.x, q.y, q.z, q.w),
        JPH::EActivation::Activate);
}

Vec3 JoltPhysicsBody::GetRotation() const {
    // Euler angles are computed from quat for backward compatibility
    JPH::Quat q = GetBodyInterface().GetRotation(m_BodyID);
    glm::quat gq(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    glm::vec3 euler = glm::degrees(glm::eulerAngles(gq));
    return Vec3(euler.x, euler.y, euler.z);
}

Quat JoltPhysicsBody::GetRotationQuat() const {
    JPH::Quat q = GetBodyInterface().GetRotation(m_BodyID);
    return Quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
}

void JoltPhysicsBody::SetRotationQuat(const Quat& q) {
    GetBodyInterface().SetRotation(m_BodyID, JPH::Quat(q.x, q.y, q.z, q.w), JPH::EActivation::Activate);
}

Mat4 JoltPhysicsBody::GetWorldMatrix() const {
    JPH::Mat44 m = GetBodyInterface().GetWorldTransform(m_BodyID);
    Mat4 result;
    std::memcpy(result.data, &m, sizeof(float32) * 16);
    return result;
}

void JoltPhysicsBody::SetLinearVelocity(const Vec3& velocity) {
    GetBodyInterface().SetLinearVelocity(m_BodyID,
        JPH::Vec3(velocity.x, velocity.y, velocity.z));
}

Vec3 JoltPhysicsBody::GetLinearVelocity() const {
    JPH::Vec3 v = GetBodyInterface().GetLinearVelocity(m_BodyID);
    return Vec3(v.GetX(), v.GetY(), v.GetZ());
}

void JoltPhysicsBody::SetAngularVelocity(const Vec3& omega) {
    GetBodyInterface().SetAngularVelocity(m_BodyID,
        JPH::Vec3(omega.x, omega.y, omega.z));
}

Vec3 JoltPhysicsBody::GetAngularVelocity() const {
    JPH::Vec3 w = GetBodyInterface().GetAngularVelocity(m_BodyID);
    return Vec3(w.GetX(), w.GetY(), w.GetZ());
}

void JoltPhysicsBody::ApplyForce(const Vec3& force, const Vec3& point) {
    GetBodyInterface().AddForce(m_BodyID,
        JPH::Vec3(force.x, force.y, force.z),
        JPH::RVec3(point.x, point.y, point.z));
}

void JoltPhysicsBody::ApplyForceAtCenter(const Vec3& force) {
    GetBodyInterface().AddForce(m_BodyID,
        JPH::Vec3(force.x, force.y, force.z));
}

void JoltPhysicsBody::ApplyImpulse(const Vec3& impulse, const Vec3& point) {
    GetBodyInterface().AddImpulse(m_BodyID,
        JPH::Vec3(impulse.x, impulse.y, impulse.z),
        JPH::RVec3(point.x, point.y, point.z));
}

void JoltPhysicsBody::SetType(BodyType3D type) {
    JPH::EMotionType mt;
    switch (type) {
        case BodyType3D::Static:    mt = JPH::EMotionType::Static; break;
        case BodyType3D::Kinematic: mt = JPH::EMotionType::Kinematic; break;
        default:                    mt = JPH::EMotionType::Dynamic; break;
    }
    GetBodyInterface().SetMotionType(m_BodyID, mt, JPH::EActivation::Activate);
}

BodyType3D JoltPhysicsBody::GetType() const {
    switch (GetBodyInterface().GetMotionType(m_BodyID)) {
        case JPH::EMotionType::Static:    return BodyType3D::Static;
        case JPH::EMotionType::Kinematic: return BodyType3D::Kinematic;
        default:                          return BodyType3D::Dynamic;
    }
}

float32 JoltPhysicsBody::GetMass() const {
    // Jolt v5.5: BodyInterface::GetMass() 已移除，通过 BodyLock 读取
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockRead lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        if (body.GetMotionProperties()) {
            return 1.0f / body.GetMotionProperties()->GetInverseMass();
        }
    }
    return 0.0f;
}

float32 JoltPhysicsBody::GetInertia() const {
    // Jolt v5.5: GetInverseInertiaDiagonal() 已移除，使用 GetInverseInertia()
    JPH::Mat44 invInertia = GetBodyInterface().GetInverseInertia(m_BodyID);
    // 取对角线最大值
    JPH::Vec3 diagonal(invInertia(0, 0), invInertia(1, 1), invInertia(2, 2));
    return 1.0f / std::max({diagonal.GetX(), diagonal.GetY(), diagonal.GetZ()});
}

Mat4 JoltPhysicsBody::GetInertiaTensor() const {
    return Mat4(); // Jolt 不建议直接获取惯性张量矩阵
}

void JoltPhysicsBody::SetLinearDamping(float32 damping) {
    // Jolt v5.5: 通过 MotionProperties 设置阻尼
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockWrite lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded()) {
        JPH::Body& body = lock.GetBody();
        if (body.GetMotionProperties()) {
            body.GetMotionProperties()->SetLinearDamping(damping);
        }
    }
}

float32 JoltPhysicsBody::GetLinearDamping() const {
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockRead lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        if (body.GetMotionProperties()) {
            return body.GetMotionProperties()->GetLinearDamping();
        }
    }
    return 0.0f;
}

void JoltPhysicsBody::SetAngularDamping(float32 damping) {
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockWrite lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded()) {
        JPH::Body& body = lock.GetBody();
        if (body.GetMotionProperties()) {
            body.GetMotionProperties()->SetAngularDamping(damping);
        }
    }
}

float32 JoltPhysicsBody::GetAngularDamping() const {
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockRead lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        if (body.GetMotionProperties()) {
            return body.GetMotionProperties()->GetAngularDamping();
        }
    }
    return 0.0f;
}

void JoltPhysicsBody::SetMaxLinearVelocity(float32 maxVel) {
    GetBodyInterface().SetMaxLinearVelocity(m_BodyID, maxVel);
}

float32 JoltPhysicsBody::GetMaxLinearVelocity() const {
    return GetBodyInterface().GetMaxLinearVelocity(m_BodyID);
}

void JoltPhysicsBody::SetMaxAngularVelocity(float32 maxVel) {
    GetBodyInterface().SetMaxAngularVelocity(m_BodyID, maxVel);
}

float32 JoltPhysicsBody::GetMaxAngularVelocity() const {
    return GetBodyInterface().GetMaxAngularVelocity(m_BodyID);
}

void* JoltPhysicsBody::AddFixture(const FixtureDef3D& def) {
    // 简化：Jolt 原生使用 BodyCreationSettings 添加形状
    // 运行时添加形状需要 CreateShape + AddConstraint
    // 这里返回 nullptr 表示暂不支持运行时添加
    (void)def;
    return nullptr;
}

void JoltPhysicsBody::RemoveFixture(void*) {}
void JoltPhysicsBody::ClearFixtures() {}

void JoltPhysicsBody::SetCollisionFilter(uint16, uint16, int32) {}

void JoltPhysicsBody::SetActive(bool active) {
    if (active)
        GetBodyInterface().ActivateBody(m_BodyID);
    else
        GetBodyInterface().DeactivateBody(m_BodyID);
}

bool JoltPhysicsBody::IsActive() const {
    return GetBodyInterface().IsActive(m_BodyID);
}

bool JoltPhysicsBody::IsSleeping() const {
    return !GetBodyInterface().IsActive(m_BodyID);
}

void JoltPhysicsBody::SetSleeping(bool sleep) {
    if (sleep) GetBodyInterface().DeactivateBody(m_BodyID);
    else GetBodyInterface().ActivateBody(m_BodyID);
}

void JoltPhysicsBody::SetUserData(void* data) {
    GetBodyInterface().SetUserData(m_BodyID, reinterpret_cast<uint64>(data));
}

void* JoltPhysicsBody::GetUserData() const {
    return reinterpret_cast<void*>(GetBodyInterface().GetUserData(m_BodyID));
}

void JoltPhysicsBody::SetComponentRef(void* ref) {
    m_ComponentRef = ref;
}

void* JoltPhysicsBody::GetComponentRef() const {
    return m_ComponentRef;
}

void* JoltPhysicsBody::GetNativeBody() {
    // Jolt v5.5: FindBody() 已移除，使用 BodyLockRead
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockWrite lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded()) {
        return static_cast<void*>(&lock.GetBody());
    }
    return nullptr;
}

} // namespace Engine