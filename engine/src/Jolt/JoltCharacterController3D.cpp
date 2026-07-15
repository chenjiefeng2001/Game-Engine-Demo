/**
 * @file JoltCharacterController3D.cpp
 * @brief Jolt Physics 角色控制器实现 — v6.0 (适配 Jolt v5.5.0)
 *
 * 封装 JPH::CharacterVirtual，实现平滑移动、爬坡、爬楼梯、防卡墙。
 * 使用形状投射（Shape Cast）而非传统刚体驱动。
 */

#include "Engine/Jolt/JoltCharacterController3D.h"
#include "Engine/Core/Physics/IPhysicsWorld3D.h"
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

namespace Engine {

JoltCharacterController3D::JoltCharacterController3D() = default;

JoltCharacterController3D::~JoltCharacterController3D() {
    delete m_Character;
    m_Character = nullptr;
}

bool JoltCharacterController3D::Init(const CharacterControllerDef& def, IPhysicsWorld3D* world) {
    m_Def = def;

    // 1. 创建胶囊体形状（半高 = height * 0.5）
    m_Shape = new JPH::CapsuleShape(def.height * 0.5f, def.radius);

    // 2. 配置 JPH::CharacterVirtualSettings
    // 注意：Jolt v5.5.0 的 CharacterVirtualSettings 没有 mFriction 字段
    // 摩擦通过 PhysicsSystem 的 ContactListener 全局配置
    JPH::CharacterVirtualSettings settings;
    settings.mShape = m_Shape;
    settings.mMass = def.mass;
    settings.mMaxStrength = def.maxStrength;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;

    // 3. 获取 Jolt PhysicsSystem 引用
    JPH::PhysicsSystem* physicsSystem = static_cast<JPH::PhysicsSystem*>(world->GetNativeWorld());
    if (!physicsSystem) return false;

    // 4. 创建角色
    JPH::RVec3 position(def.position.x, def.position.y, def.position.z);
    JPH::Quat rotation = JPH::Quat::sIdentity();
    m_Character = new JPH::CharacterVirtual(&settings, position, rotation, 0, physicsSystem);

    // 5. 配置最大爬坡角度
    m_Character->SetMaxSlopeAngle(JPH::DegreesToRadians(def.maxSlopeAngle));

    return true;
}

void JoltCharacterController3D::Move(const Vec3& velocity, float32 dt) {
    if (!m_Character) return;

    // 应用水平速度
    JPH::Vec3 vel(velocity.x, m_VerticalVelocity, velocity.z);
    m_Character->SetLinearVelocity(vel);

    // v5.5.0 的 ExtendedUpdate 使用嵌套结构体配置
    // 与后期版本不同，v5.5.0 没有 mStickToFloor/mWalkStairs 快捷字段
    // ExtendedUpdate 直接接收各个参数
    m_Character->ExtendedUpdate(
        dt,
        m_Character->GetUp(),       // up direction
        JPH::CharacterVirtual::ExtendedUpdateSettings(), // 默认设置
        JPH::BroadPhaseLayerFilter(),
        JPH::ObjectLayerFilter(),
        JPH::BodyFilter(),
        JPH::ShapeFilter(),
        *m_Character->GetActiveContacts(),
        *JPH::TempAllocator::GetTempAllocator()
    );

    // 重力
    m_VerticalVelocity += -9.81f * dt;

    // v5.5.0: GetGroundState() == GroundState::OnGround 替代 IsGrounded()
    if (m_Character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround) {
        m_VerticalVelocity = 0.0f;
    }
}

void JoltCharacterController3D::Jump(float32 force) {
    if (!m_Character) return;
    // v5.5.0: 使用 GetGroundState() 替代 IsGrounded()
    if (m_Character->GetGroundState() != JPH::CharacterVirtual::EGroundState::OnGround) return;
    m_VerticalVelocity = force;
}

bool JoltCharacterController3D::IsGrounded() const {
    // v5.5.0: GetGroundState() 替代 IsGrounded()
    return m_Character && m_Character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

Vec3 JoltCharacterController3D::GetPosition() const {
    if (!m_Character) return m_Def.position;
    JPH::RVec3 pos = m_Character->GetPosition();
    return Vec3(pos.GetX(), pos.GetY(), pos.GetZ());
}

void JoltCharacterController3D::SetPosition(const Vec3& pos) {
    if (m_Character) {
        m_Character->SetPosition(JPH::RVec3(pos.x, pos.y, pos.z));
    }
}

Vec3 JoltCharacterController3D::GetVelocity() const {
    if (!m_Character) return Vec3(0, 0, 0);
    JPH::Vec3 vel = m_Character->GetLinearVelocity();
    return Vec3(vel.GetX(), vel.GetY(), vel.GetZ());
}

void JoltCharacterController3D::SetVelocity(const Vec3& vel) {
    if (m_Character) {
        m_Character->SetLinearVelocity(JPH::Vec3(vel.x, vel.y, vel.z));
    }
}

float JoltCharacterController3D::GetVerticalVelocity() const {
    return m_VerticalVelocity;
}

} // namespace Engine