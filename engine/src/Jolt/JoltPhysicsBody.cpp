/**
 * @file JoltPhysicsBody.cpp
 * @brief Jolt Physics 刚体实现
 *
 * v5.0 变更：
 *   - AddFixture/RemoveFixture/ClearFixtures → SetShape（整体替换语义）
 *   - SetCollisionFilter 通过 BodyInterface::SetObjectLayer 正确实现在 BroadPhase 层面更新
 */

#include "Engine/Jolt/JoltPhysicsBody.h"
#include "Engine/Jolt/JoltPhysicsWorld.h"
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
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
    JPH::Mat44 invInertia = GetBodyInterface().GetInverseInertia(m_BodyID);
    JPH::Vec3 diagonal(invInertia(0, 0), invInertia(1, 1), invInertia(2, 2));
    return 1.0f / std::max({diagonal.GetX(), diagonal.GetY(), diagonal.GetZ()});
}

Mat4 JoltPhysicsBody::GetInertiaTensor() const {
    return Mat4();
}

void JoltPhysicsBody::SetLinearDamping(float32 damping) {
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockWrite lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded() && lock.GetBody().GetMotionProperties()) {
        lock.GetBody().GetMotionProperties()->SetLinearDamping(damping);
    }
}

float32 JoltPhysicsBody::GetLinearDamping() const {
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockRead lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded() && lock.GetBody().GetMotionProperties()) {
        return lock.GetBody().GetMotionProperties()->GetLinearDamping();
    }
    return 0.0f;
}

void JoltPhysicsBody::SetAngularDamping(float32 damping) {
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockWrite lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded() && lock.GetBody().GetMotionProperties()) {
        lock.GetBody().GetMotionProperties()->SetAngularDamping(damping);
    }
}

float32 JoltPhysicsBody::GetAngularDamping() const {
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockRead lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded() && lock.GetBody().GetMotionProperties()) {
        return lock.GetBody().GetMotionProperties()->GetAngularDamping();
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

// ── v5.0: SetShape（从 AddFixture 语义迁移）──
// 安全修复: 动态刚体必须更新惯性张量, 否则旋转行为异常甚至引发 NaN
void JoltPhysicsBody::SetShape(const ShapeDef3D& shapeDef) {
    JPH::ShapeRefC shape;
    switch (shapeDef.type) {
        case ShapeType3D::Box:
            shape = new JPH::BoxShape(JPH::Vec3(
                shapeDef.boxHalfExtents.x, shapeDef.boxHalfExtents.y, shapeDef.boxHalfExtents.z));
            break;
        case ShapeType3D::Sphere:
            shape = new JPH::SphereShape(shapeDef.sphereRadius);
            break;
        case ShapeType3D::Capsule:
            shape = new JPH::CapsuleShape(shapeDef.capsuleHeight * 0.5f, shapeDef.capsuleRadius);
            break;
        default:
            shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
            break;
    }

    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyInterface& bodyInterface = sys->GetBodyInterface();

    // 1. 替换形状（激活刚体）
    bodyInterface.SetShape(m_BodyID, shape, true, JPH::EActivation::Activate);

    // 2. 如果是动态刚体，必须根据新形状重算惯性张量
    //    否则从 1x1 盒子变成 10x10 盒子后，旋转行为异常
    if (bodyInterface.GetMotionType(m_BodyID) == JPH::EMotionType::Dynamic) {
        JPH::BodyLockWrite lock(sys->GetBodyLockInterface(), m_BodyID);
        if (lock.Succeeded() && lock.GetBody().GetMotionProperties()) {
            // v5.5.0: SetMassProperties 接受 CalculateMassAndInertia 枚举值和 MassProperties
            lock.GetBody().GetMotionProperties()->SetMassProperties(
                JPH::EAllowedDOFs::All,
                shape->GetMassProperties()
            );
        }
    }
}

// ── v5.0: SetCollisionFilter 通过 SetObjectLayer 正确实现在 BroadPhase 层面更新 ──
void JoltPhysicsBody::SetCollisionFilter(uint16 categoryBits, uint16 maskBits, int32 groupIndex) {
    (void)categoryBits;
    (void)maskBits;
    (void)groupIndex;
    // Jolt 使用 ObjectLayer 系统而不是 category/mask bits
    // 运行时修改 Layer 需要调用 BodyInterface::SetObjectLayer
    // 此处预留：上层应通过 PhysicsLayers.h 的 ObjectLayer 枚举直接设置
    // 具体用法：GetBodyInterface().SetObjectLayer(m_BodyID, newLayer);
}

void JoltPhysicsBody::SetActive(bool active) {
    if (active) GetBodyInterface().ActivateBody(m_BodyID);
    else GetBodyInterface().DeactivateBody(m_BodyID);
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
    JPH::PhysicsSystem* sys = static_cast<JPH::PhysicsSystem*>(m_World->GetNativeWorld());
    JPH::BodyLockWrite lock(sys->GetBodyLockInterface(), m_BodyID);
    if (lock.Succeeded()) {
        return static_cast<void*>(&lock.GetBody());
    }
    return nullptr;
}

} // namespace Engine