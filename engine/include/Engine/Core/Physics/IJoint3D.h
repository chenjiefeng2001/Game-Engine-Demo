#pragma once

/**
 * @file IJoint3D.h
 * @brief 3D 关节接口 — 不依赖任何第三方物理库
 *
 * 支持 3D 物理引擎中的常见关节类型：
 *   - 固定关节 (Fixed)
 *   - 旋转关节 (Hinge)
 *   - 滑动关节 (Slider/Prismatic)
 *   - 球窝关节 (Ball/Socket)
 *   - 距离关节 (Distance)
 *   - 弹簧关节 (Spring)
 */

#include "Engine/Core/Physics/PhysicsDefs3D.h"
#include <memory>

namespace Engine {

    class IPhysicsBody3D;

    // ════════════════════════════════════════════════
    // 关节类型
    // ════════════════════════════════════════════════
    enum class JointType3D : uint8 {
        Fixed,
        Hinge,      ///< 绕单轴旋转（如门铰链）
        Slider,     ///< 沿单轴滑动
        Ball,       ///< 球窝（绕任意轴旋转）
        Distance,   ///< 固定两点距离
        Spring,     ///< 弹簧
        SixDOF      ///< 六个自由度全可控
    };

    // ════════════════════════════════════════════════
    // 关节限制
    // ════════════════════════════════════════════════
    struct JointLimits3D {
        bool  enableMin = false;
        bool  enableMax = false;
        float32 min = 0.0f;
        float32 max = 0.0f;
    };

    // ════════════════════════════════════════════════
    // 弹簧参数
    // ════════════════════════════════════════════════
    struct SpringParams3D {
        float32 stiffness   = 100.0f;
        float32 damping     = 1.0f;
        float32 restLength  = 1.0f;
    };

    // ════════════════════════════════════════════════
    // 关节定义基类
    // ════════════════════════════════════════════════
    struct JointDef3D {
        JointType3D type = JointType3D::Fixed;

        IPhysicsBody3D* bodyA = nullptr;
        IPhysicsBody3D* bodyB = nullptr;

        // 在两个物体上的连接点（世界空间）
        Vec3 anchorPointA = {0, 0, 0};
        Vec3 anchorPointB = {0, 0, 0};

        // Hinge: 旋转轴
        Vec3 hingeAxis = {0, 0, 1};

        // Slider: 滑动轴
        Vec3 sliderAxis = {1, 0, 0};

        // Distance: 目标距离
        float32 distance = 1.0f;

        // Spring: 弹簧参数
        SpringParams3D spring;

        // 限制
        JointLimits3D limits;

        // 马达
        bool   enableMotor = false;
        float32 motorSpeed = 0.0f;
        float32 maxMotorTorque = 100.0f;
    };

    // ════════════════════════════════════════════════
    // 关节接口
    // ════════════════════════════════════════════════
    class IJoint3D {
    public:
        virtual ~IJoint3D() = default;

        virtual JointType3D GetType() const = 0;

        virtual void SetLimits(const JointLimits3D& limits) = 0;
        virtual JointLimits3D GetLimits() const = 0;

        virtual void EnableMotor(bool enable) = 0;
        virtual bool IsMotorEnabled() const = 0;
        virtual void SetMotorSpeed(float32 speed) = 0;
        virtual float32 GetMotorSpeed() const = 0;
        virtual void SetMaxMotorTorque(float32 torque) = 0;
        virtual float32 GetMaxMotorTorque() const = 0;

        virtual float32 GetCurrentAngle() const = 0;  // Hinge 专用
        virtual float32 GetCurrentDistance() const = 0; // Distance 专用

        virtual IPhysicsBody3D* GetBodyA() const = 0;
        virtual IPhysicsBody3D* GetBodyB() const = 0;

        virtual void* GetNativeJoint() = 0;
    };

} // namespace Engine
