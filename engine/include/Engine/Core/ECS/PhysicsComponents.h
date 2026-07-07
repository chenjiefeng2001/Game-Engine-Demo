#pragma once

/**
 * @file PhysicsComponents.h
 * @brief ECS 3D 物理组件 — 数据与运行期状态分离
 *
 * 设计原则（v3.1）：
 *   - RigidBody3DComponent: 开发人员配置数据（编辑器可修改）
 *   - PhysicsRuntimeComponent: 物理系统内部运行期状态（不暴露给编辑器）
 *   - 配置数据修改触发 Resync，运行期状态由物理系统自动管理
 *
 *   TransformComponent 永远只存储物理积分后的"真实"坐标。
 *   渲染插值结果只存在于渲染系统的局部变量中，不写回任何 ECS 组件。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/Physics/PhysicsLayers.h"
#include <cstdint>

namespace Engine {

// ════════════════════════════════════════════════════════
// 组件 A：配置数据（编辑器可序列化/修改）
// ════════════════════════════════════════════════════════
struct RigidBody3DComponent {
    enum class MotionType : uint8 {
        Static    = 0,   // 静止：不受力影响，质量无限大
        Kinematic = 1,   // 运动学：由用户控制运动，不受力影响
        Dynamic   = 2,   // 动态：受力和碰撞影响
    };

    MotionType motionType = MotionType::Dynamic;
    ObjectLayer layer = ObjectLayer::MOVING;

    float mass          = 1.0f;
    float friction      = 0.5f;
    float restitution   = 0.0f;
    float linearDamping  = 0.01f;
    float angularDamping = 0.05f;

    bool allowSleep     = true;
    bool useCCD         = false;   // 连续碰撞检测（高速物体）
    bool disableGravity = false;
    bool isTrigger      = false;  // 触发器（仅通知，无物理响应）
};

// ════════════════════════════════════════════════════════
// 组件 B：运行期状态（物理系统内部使用）
// ════════════════════════════════════════════════════════
struct PhysicsRuntimeComponent {
    uint64 runtimeBodyID = 0;     // Jolt BodyID（封装在 uint64 中避免暴露头文件）

    // 双缓冲位置（用于渲染插值, 由 PhysicsSyncSystem 维护）
    // ★ 不写回 TransformComponent，仅供渲染系统读取
    Vec3 prevPosition  = {0, 0, 0};
    Quat prevRotation  = Quat::Identity(); // 四元数，支持 Slerp 插值

    bool isActive      = false;   // 物体是否活跃（非休眠）
    bool isDirty       = false;   // 标记需要重新同步到物理引擎（如脚本/编辑器修改）
};

// ════════════════════════════════════════════════════════
// 3D 碰撞体形状组件
// ════════════════════════════════════════════════════════

struct BoxCollider3DComponent {
    Vec3 halfExtents  = {0.5f, 0.5f, 0.5f};
    Vec3 offset       = {0.0f, 0.0f, 0.0f};
    float density     = 1.0f;
    float friction    = 0.5f;
    float restitution = 0.2f;
    bool isSensor     = false;
    bool isDirty      = false;   ///< v4.0: 标记需要重新创建 Shape 到物理引擎

    void MarkDirty() { isDirty = true; }
};

struct SphereCollider3DComponent {
    float radius = 0.5f;
    Vec3 offset  = {0.0f, 0.0f, 0.0f};
    float density     = 1.0f;
    float friction    = 0.5f;
    float restitution = 0.2f;
    bool isSensor     = false;
    bool isDirty      = false;   ///< v4.0

    void MarkDirty() { isDirty = true; }
};

struct CapsuleCollider3DComponent {
    float radius = 0.25f;
    float height = 1.0f;
    Vec3 offset  = {0.0f, 0.0f, 0.0f};
    float density     = 1.0f;
    float friction    = 0.5f;
    float restitution = 0.2f;
    bool isSensor     = false;
    bool isDirty      = false;   ///< v4.0

    void MarkDirty() { isDirty = true; }
};

} // namespace Engine