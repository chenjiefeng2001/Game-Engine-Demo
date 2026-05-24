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

    class IPhysicsBody;  // 前向声明（JointDef 中使用了 IPhysicsBody*）

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
    Circle,
    Edge,       ///< 线段（两个端点）
    Chain       ///< 链状（多个顶点，首尾不相连）
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

    // Edge: 两个端点
    Vec2 edgeStart = {-0.5f, 0.0f};
    Vec2 edgeEnd   = { 0.5f, 0.0f};

    // Chain: 顶点列表
    const Vec2* chainVertices = nullptr;
    int32       chainVertexCount = 0;

    // 相对于物体质心的偏移
    Vec2 offset = {0.0f, 0.0f};

    // 传感器标记（只触发碰撞回调，不产生物理碰撞）
    bool isSensor = false;
};

// ============================================================
// Fixture 定义（用于运行时添加额外的形状）
// ============================================================
struct FixtureDef {
    ShapeDef  shape;
    float32   density     = 1.0f;
    float32   friction    = 0.3f;
    float32   restitution = 0.2f;
    bool      isSensor    = false;
    uint16    categoryBits = 0x0001;
    uint16    maskBits     = 0xFFFF;
    int32     groupIndex   = 0;
    void*     userData     = nullptr;
};

// ============================================================
// 刚体定义
// ============================================================
struct BodyDef {
    BodyType type = BodyType::Dynamic;

    // 初始位置和角度（世界坐标）
    Vec2   position = {0.0f, 0.0f};
    float32  angle    = 0.0f;  // 弧度

    // 形状（创建时添加的第一个 Fixture）
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
// 关节类型
// ============================================================
enum class JointType : uint8 {
    Unknown,
    Revolute,   
    Prismatic,  
    Distance,   
    Weld,       
    Wheel,      
    Mouse      
};


struct JointDef {
    JointType     type      = JointType::Unknown;
    IPhysicsBody* bodyA     = nullptr;
    IPhysicsBody* bodyB     = nullptr;
    bool          collideConnected = false;
    void*         userData  = nullptr;
};

/// 旋转关节（铰链）
struct RevoluteJointDef : JointDef {
    RevoluteJointDef() { type = JointType::Revolute; }

    Vec2    localAnchorA  = {0.0f, 0.0f};  
    Vec2    localAnchorB  = {0.0f, 0.0f};  
    float32 referenceAngle = 0.0f;         

    bool    enableLimit    = false;
    float32 lowerAngle     = 0.0f;         
    float32 upperAngle     = 0.0f;         

    bool    enableMotor    = false;
    float32 motorSpeed     = 0.0f;
    float32 maxMotorTorque = 0.0f;
};

/// 滑动关节（棱柱）
struct PrismaticJointDef : JointDef {
    PrismaticJointDef() { type = JointType::Prismatic; }

    Vec2    localAnchorA   = {0.0f, 0.0f};
    Vec2    localAnchorB   = {0.0f, 0.0f};
    Vec2    localAxisA     = {1.0f, 0.0f};  
    float32 referenceAngle = 0.0f;

    bool    enableLimit    = false;
    float32 lowerTranslation = 0.0f;        
    float32 upperTranslation = 0.0f;        
    bool    enableMotor    = false;
    float32 motorSpeed     = 0.0f;
    float32 maxMotorForce  = 0.0f;
};

struct DistanceJointDef : JointDef {
    DistanceJointDef() { type = JointType::Distance; }

    Vec2    localAnchorA  = {0.0f, 0.0f};
    Vec2    localAnchorB  = {0.0f, 0.0f};
    float32 length        = 1.0f;            
    float32 stiffness     = 100.0f;        
    float32 damping       = 1.0f;          
};


struct WeldJointDef : JointDef {
    WeldJointDef() { type = JointType::Weld; }

    Vec2    localAnchorA  = {0.0f, 0.0f};
    Vec2    localAnchorB  = {0.0f, 0.0f};
    float32 referenceAngle = 0.0f;
};


struct WheelJointDef : JointDef {
    WheelJointDef() { type = JointType::Wheel; }

    Vec2    localAnchorA  = {0.0f, 0.0f};
    Vec2    localAnchorB  = {0.0f, 0.0f};
    Vec2    localAxisA    = {0.0f, 1.0f};

    bool    enableMotor    = false;
    float32 motorSpeed     = 0.0f;
    float32 maxMotorTorque = 0.0f;

    float32 stiffness     = 100.0f;
    float32 damping       = 1.0f;
};


struct MouseJointDef : JointDef {
    MouseJointDef() { type = JointType::Mouse; bodyB = nullptr; }

    IPhysicsBody* bodyA     = nullptr;  
    Vec2          target    = {0.0f, 0.0f};  
    float32       maxForce  = 1000.0f;  
    float32       stiffness = 100.0f;    
    float32       damping   = 1.0f;     
};

// ============================================================
// 碰撞事件信息
// ============================================================
struct ContactInfo {
    const void* bodyA = nullptr;  
    const void* bodyB = nullptr;
    Vec2  point;                  
    Vec2  normal;                 
    float32 impulse = 0.0f;         
};

/// 碰撞事件冲量信息（用于持久回调 PostSolve）
struct ContactPersistData {
    const void* bodyA  = nullptr;
    const void* bodyB  = nullptr;
    Vec2        point;           
    Vec2        normal;          
    float32     impulse = 0.0f;    
    int32       pointCount = 0;    
    float32     impulses[4] = {};  
};

/// 碰撞开始/结束/持久回调
using ContactCallback       = std::function<void(const ContactInfo& info)>;
using ContactPersistCallback = std::function<void(const ContactPersistData& data)>;

/// 碰撞滤波回调（返回 true 允许碰撞）
using ContactFilterCallback = std::function<bool(const void* bodyA,
                                                  const void* bodyB)>;

// ============================================================
// 光线投射结果
// ============================================================
struct RayCastResult {
    const void* body = nullptr;   
    Vec2  point;                  
    Vec2  normal;                 
    float32 fraction = 1.0f;        
};
} // namespace Engine
