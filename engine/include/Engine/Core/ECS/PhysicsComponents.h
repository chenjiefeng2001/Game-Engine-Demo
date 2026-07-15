#pragma once

/**
 * @file PhysicsComponents.h
 * @brief ECS 3D 物理组件 — 数据与运行期状态分离
 *
 * 设计原则（v6.0）：
 *   - RigidBody3DComponent: 开发人员配置数据（编辑器可修改）
 *   - PhysicsRuntimeComponent: 物理系统内部运行期状态（不暴露给编辑器）
 *   - Joint3DComponent: 连接两个实体的关节定义（v6.0 新增）
 *   - CollisionListenerComponent: 碰撞事件回调（v6.0 新增）
 *   - 配置数据修改触发 Resync，运行期状态由物理系统自动管理
 *
 *   TransformComponent 永远只存储物理积分后的"真实"坐标。
 *   渲染插值结果只存在于渲染系统的局部变量中，不写回任何 ECS 组件。
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/Physics/PhysicsLayers.h"
#include "Engine/Core/Physics/IJoint3D.h"
#include "Engine/Core/ECS/ECS.fwd.h"
#include <cstdint>
#include <functional>

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

// ════════════════════════════════════════════════════════
// v6.0: 关节组件（连接两个实体的约束定义）
// ════════════════════════════════════════════════════════
struct Joint3DComponent {
    EntityHandle entityA = {};          ///< 第一个实体
    EntityHandle entityB = {};          ///< 第二个实体
    JointType3D  jointType = JointType3D::Fixed;

    Vec3 anchorPointA = {0, 0, 0};     ///< 实体A上的连接点（世界空间）
    Vec3 anchorPointB = {0, 0, 0};     ///< 实体B上的连接点（世界空间）

    // Hinge: 旋转轴
    Vec3 hingeAxis = {0, 0, 1};

    // Slider: 滑动轴
    Vec3 sliderAxis = {1, 0, 0};

    // Distance: 目标距离
    float distance = 1.0f;

    // 限制
    JointLimits3D limits;

    // 马达
    bool   enableMotor    = false;
    float  motorSpeed     = 0.0f;
    float  maxMotorTorque = 100.0f;
};

// ════════════════════════════════════════════════════════
// v6.0: 碰撞事件监听组件（接收 OnCollisionEnter/Exit 回调）
// ════════════════════════════════════════════════════════
struct CollisionListenerComponent {
    /// 碰撞开始时触发（otherBodyID: 碰撞对方的 BodyID）
    std::function<void(uint64 otherBodyID, const Vec3& point)> onCollisionEnter;

    /// 碰撞结束时触发
    std::function<void(uint64 otherBodyID)> onCollisionExit;
};

} // namespace Engine