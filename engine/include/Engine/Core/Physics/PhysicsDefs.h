#pragma once

/**
 * @file PhysicsDefs.h
 * @brief 物理系统纯数据结构 — 不依赖任何第三方物理库
 *
 * 设计原则（与 RHI/MathTypes.h 一致）：
 *   - 头文件只依赖 Engine/Types.h 和 RHI/MathTypes.h
 *   - 所有类型都是 POD 或轻量值类型
 *   - 物理系统外层逻辑只依赖此文件中的定义
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <functional>

namespace Engine {

// ============================================================
// 刚体类型
// ============================================================
enum class BodyType : uint8 {
    Static,     ///< 静态：不受力影响，质量无限大
    Dynamic,    ///< 动态：受力和碰撞影响
    Kinematic   ///< 运动学：由用户控制速度，不受力影响
};

// ============================================================
// 形状类型
// ============================================================
enum class ShapeType : uint8 {
    Box,
    Circle
};

// ============================================================
// 形状定义
// ============================================================
struct ShapeDef {
    ShapeType type = ShapeType::Box;

    // Box: 半宽半高
    Vec2 boxSize = {0.5f, 0.5f};

    // Circle: 半径
    float32 circleRadius = 0.5f;

    // 相对于物体质心的偏移
    Vec2 offset = {0.0f, 0.0f};
};

// ============================================================
// 刚体定义
// ============================================================
struct BodyDef {
    BodyType type = BodyType::Dynamic;

    // 初始位置和角度（世界坐标）
    Vec2   position = {0.0f, 0.0f};
    float32  angle    = 0.0f;  // 弧度

    // 形状
    ShapeDef shape;

    // 物理材质属性
    float32 density     = 1.0f;
    float32 friction    = 0.3f;
    float32 restitution = 0.2f;  // 弹性 (0~1)

    // 线性阻尼和角阻尼
    float32 linearDamping  = 0.0f;
    float32 angularDamping = 0.0f;

    // 固定旋转（用于角色等不应旋转的物体）
    bool fixedRotation = false;

    // 允许休眠（静态物体建议 true）
    bool allowSleep = true;

    // 碰撞滤波
    uint16 categoryBits = 0x0001;
    uint16 maskBits     = 0xFFFF;
    int32  groupIndex   = 0;

    // 用户数据（用于碰撞回调识别）
    void* userData = nullptr;
};

// ============================================================
// 碰撞事件信息
// ============================================================
struct ContactInfo {
    const void* bodyA = nullptr;   ///< IPhysicsBody* 的原始指针
    const void* bodyB = nullptr;
    Vec2  point;                   ///< 碰撞点（世界坐标）
    Vec2  normal;                  ///< 法线方向（从 A 指向 B）
    float32 impulse = 0.0f;          ///< 碰撞冲量大小
};

/// 碰撞开始/结束回调
using ContactCallback = std::function<void(const ContactInfo& info)>;

/// 碰撞滤波回调（返回 true 允许碰撞）
using ContactFilterCallback = std::function<bool(const void* bodyA,
                                                  const void* bodyB)>;

// ============================================================
// 光线投射结果
// ============================================================
struct RayCastResult {
    const void* body = nullptr;    ///< IPhysicsBody* 的原始指针
    Vec2  point;                   ///< 交点
    Vec2  normal;                  ///< 交点法线
    float32 fraction = 1.0f;         ///< 0~1，沿射线的位置
};

} // namespace Engine
