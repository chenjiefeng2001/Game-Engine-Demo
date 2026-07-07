#pragma once

/**
 * @file JoltCharacterController3D.h
 * @brief Jolt Physics 角色控制器实现 — v6.0 Gameplay 赋能
 *
 * 封装 JPH::CharacterVirtual（虚拟角色控制器），而非传统刚体。
 * 虚拟角色依靠射线和形状投射（Shape Cast）计算移动，防穿模、爬楼梯。
 * 不创建真实物理实体，位置单向从物理系统同步到 TransformComponent。
 */

#include "Engine/Core/Physics/ICharacterController3D.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace Engine {

class JoltCharacterController3D final : public ICharacterController3D {
public:
    JoltCharacterController3D();
    ~JoltCharacterController3D() override;

    bool Init(const CharacterControllerDef& def, IPhysicsWorld3D* world) override;
    void Move(const Vec3& velocity, float32 dt) override;
    void Jump(float32 force) override;
    bool IsGrounded() const override;
    Vec3 GetPosition() const override;
    void SetPosition(const Vec3& pos) override;
    Vec3 GetVelocity() const override;
    void SetVelocity(const Vec3& vel) override;
    float GetVerticalVelocity() const override;
    void* GetNativeController() override { return m_Character; }

private:
    JPH::CharacterVirtual* m_Character = nullptr;
    CharacterControllerDef m_Def;
    float m_VerticalVelocity = 0.0f;
    JPH::Ref<JPH::Shape> m_Shape;
};

} // namespace Engine