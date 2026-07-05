#pragma once

/**
 * @file PhysicsDefs3D.h
 * @brief 3D 物理系统纯数据结构 — 不依赖任何第三方物理库
 *
 * 设计原则（与 2D PhysicsDefs.h 一致）：
 *   - 头文件只依赖 Engine/Types.h 和 RHI/MathTypes.h
 *   - 所有类型使用 Vec3 / Mat4，不暴露 Jolt/PhysX/Bullet 类型
 *   - 新接入 3D 物理引擎时，只需实现 IPhysicsWorld3D / IPhysicsBody3D
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <functional>
#include <memory>

namespace Engine {

    class IPhysicsBody3D;   // 前向声明

    // ════════════════════════════════════════════════
    // 刚体类型（同 2D）
    // ════════════════════════════════════════════════
    enum class BodyType3D : uint8 {
        Static,     ///< 静止：不受力影响，质量无限大
        Dynamic,    ///< 动态：受力和碰撞影响
        Kinematic   ///< 运动学：由用户控制运动，不受力影响
    };

    // ════════════════════════════════════════════════
    // 3D 形状类型
    // ════════════════════════════════════════════════
    enum class ShapeType3D : uint8 {
        Box,
        Sphere,
        Capsule,      ///< 胶囊体（球柱组合）
        Cylinder,
        ConvexHull,   ///< 凸包（用户提供顶点列表）
        Mesh,         ///< 三角网格（仅 Static，不可 Dynamic）
        Plane,        ///< 无限平面 (用于地面)
        Compound      ///< 复合形状（组合多个子形状）
    };

    // ════════════════════════════════════════════════
    // 3D 形状定义
    // ════════════════════════════════════════════════
    struct ShapeDef3D {
        ShapeType3D type = ShapeType3D::Box;

        // --- Box ---
        Vec3 boxHalfExtents = {0.5f, 0.5f, 0.5f};

        // --- Sphere ---
        float sphereRadius = 0.5f;

        // --- Capsule ---
        float capsuleRadius = 0.25f;
        float capsuleHeight = 1.0f;

        // --- Cylinder ---
        float cylinderRadius = 0.25f;
        float cylinderHeight = 1.0f;

        // --- ConvexHull / Mesh ---
        const Vec3* hullVertices   = nullptr;
        uint32      hullVertexCount = 0;
        const uint32* meshIndices    = nullptr;
        uint32        meshIndexCount = 0;

        // --- Plane ---
        Vec3 planeNormal = {0.0f, 1.0f, 0.0f};
        float planeDistance = 0.0f;  // 沿法线方向的距离

        // --- 通用属性 ---
        Vec3   offset      = {0.0f, 0.0f, 0.0f};  // 相对质心偏移
        bool   isSensor    = false;
    };

    // ════════════════════════════════════════════════
    // Fixture 定义（运行时添加额外形状）
    // ════════════════════════════════════════════════
    struct FixtureDef3D {
        ShapeDef3D shape;
        float32    density     = 1.0f;
        float32    friction    = 0.3f;
        float32    restitution = 0.2f;
        bool       isSensor    = false;
        uint16     categoryBits = 0x0001;
        uint16     maskBits     = 0xFFFF;
        int32      groupIndex   = 0;
        void*      userData     = nullptr;
    };

    // ════════════════════════════════════════════════
    // 刚体定义
    // ════════════════════════════════════════════════
    struct BodyDef3D {
        BodyType3D type = BodyType3D::Dynamic;

        Vec3   position = {0.0f, 0.0f, 0.0f};
        Vec3   rotation = {0.0f, 0.0f, 0.0f};  // 欧拉角 (pitch, yaw, roll)

        // 初始形状
        ShapeDef3D shape;

        // 物理材质
        float32 density     = 1.0f;
        float32 friction    = 0.3f;
        float32 restitution = 0.2f;

        // 阻尼
        float32 linearDamping  = 0.01f;
        float32 angularDamping = 0.01f;

        // 运动限制
        bool   allowSleep      = true;
        bool   isBullet        = false;   // CCD（连续碰撞检测）
        float32 maxLinearVelocity  = 500.0f;
        float32 maxAngularVelocity = 50.0f;

        // 碰撞分组
        uint16 categoryBits = 0x0001;
        uint16 maskBits     = 0xFFFF;
        int32  groupIndex   = 0;

        void*  userData     = nullptr;
    };

    // ════════════════════════════════════════════════
    // 碰撞结果
    // ════════════════════════════════════════════════
    struct RayCastResult3D {
        IPhysicsBody3D* body    = nullptr;
        Vec3            point;      // 世界坐标击中点
        Vec3            normal;     // 击中点法线
        float32         fraction;   // 0~1 沿射线比例
    };

    struct OverlapResult3D {
        IPhysicsBody3D* body = nullptr;
    };

    // ════════════════════════════════════════════════
    // 接触回调
    // ════════════════════════════════════════════════
    struct ContactData3D {
        IPhysicsBody3D* bodyA = nullptr;
        IPhysicsBody3D* bodyB = nullptr;
        Vec3            contactPoint;   // 世界空间接触点
        Vec3            contactNormal;  // 从 A 指向 B 的法线
        float32         penetration;    // 穿透深度
    };

    struct ContactPersistData3D {
        IPhysicsBody3D* bodyA    = nullptr;
        IPhysicsBody3D* bodyB    = nullptr;
        Vec3            totalImpulse;   // 本帧累计冲量
    };

    using ContactCallback3D        = std::function<void(ContactData3D&)>;
    using ContactFilterCallback3D  = std::function<bool(ContactData3D&)>;
    using ContactPersistCallback3D = std::function<void(ContactPersistData3D&)>;

    // ════════════════════════════════════════════════
    // 3D 物理世界配置
    // ════════════════════════════════════════════════
    struct PhysicsWorldConfig3D {
        Vec3    gravity          = {0.0f, -9.81f, 0.0f};
        int32   maxBodies        = 65536;
        int32   maxContactConstraints = 10240;
        int32   maxPairs         = 65536;
        bool    enableSleep      = true;
        bool    enableCCD        = false;   // 连续碰撞检测
    };

} // namespace Engine
