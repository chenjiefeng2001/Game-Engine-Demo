#pragma once

/**
 * @file ICharacterController3D.h
 * @brief 3D 角色控制器接口 — v6.0 Gameplay 赋能
 *
 * 设计原则：
 *   - 使用 JPH::CharacterVirtual（虚拟角色控制器），而非传统刚体。
 *   - 虚拟角色依靠射线和形状投射（Shape Cast）计算移动，防穿模、爬楼梯。
 *   - 不真实物理实体，位置单向从物理系统同步到 TransformComponent。
 *
 * 支持：
 *   - Move(velocity, dt)：基于速度的移动
 *   - Jump(force)：跳跃
 *   - IsGrounded()：是否在地面
 *   - MaxSlopeAngle / MaxStepHeight：最大爬坡角度/台阶高度
 */

#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Types.h"

namespace Engine {

    class IPhysicsWorld3D;

    // ════════════════════════════════════════════════
    // 角色控制器定义
    // ════════════════════════════════════════════════
    struct CharacterControllerDef {
        Vec3  position        = {0.0f, 1.0f, 0.0f};
        float radius          = 0.3f;     ///< 胶囊体半径
        float height          = 1.7f;     ///< 胶囊体高度（不含半球）
        float maxSlopeAngle   = 45.0f;    ///< 最大爬坡角度（度）
        float maxStepHeight   = 0.3f;     ///< 最大台阶高度
        float mass            = 80.0f;    ///< 质量（KG）
        float friction        = 0.5f;
        float linearDamping   = 0.1f;
        uint32 collisionLayer = 1;        ///< 碰撞层 ObjectLayer::MOVING
        float maxStrength     = 500.0f;   ///< 最大推动力
    };

    // ════════════════════════════════════════════════
    // 角色控制器接口
    // ════════════════════════════════════════════════
    class ICharacterController3D {
    public:
        virtual ~ICharacterController3D() = default;

        /// 初始化角色（绑定到物理世界）
        virtual bool Init(const CharacterControllerDef& def, IPhysicsWorld3D* world) = 0;

        /// 每帧移动（主线程调用）
        virtual void Move(const Vec3& velocity, float32 dt) = 0;

        /// 跳跃
        virtual void Jump(float32 force) = 0;

        /// 是否在地面上
        virtual bool IsGrounded() const = 0;

        /// 获取当前位置（物理同步后）
        virtual Vec3 GetPosition() const = 0;

        /// 设置位置（传送/复位）
        virtual void SetPosition(const Vec3& pos) = 0;

        /// 获取当前速度
        virtual Vec3 GetVelocity() const = 0;

        /// 设置速度
        virtual void SetVelocity(const Vec3& vel) = 0;

        /// 获取下降速度（用于视觉动画混合）
        virtual float GetVerticalVelocity() const = 0;

        /// 底层访问
        virtual void* GetNativeController() = 0;
    };

} // namespace Engine