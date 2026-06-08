#pragma once

/**
 * @file PhysicsDefs.h
 * @brief 物理系统纯数据结构 — 无第三方依赖
 *
 * 设计原则：
 *   - 只依赖 Engine/Types.h 和 RHI/MathTypes.h
 *   - 所有类型是 POD 或轻量值类型
 *   - 材质系统、碰撞滤波、触发器支持
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <functional>
#include <memory>
#include <vector>

namespace Engine {

    class IPhysicsBody;
    class IPhysicsWorld;

    // ============================================================
    // 刚体类型
    // ============================================================
    enum class BodyType : uint8 {
        Static,     ///< 静态：质量无限大，不受力
        Dynamic,    ///< 动态：受力和碰撞影响
        Kinematic   ///< 运动学：用户控制速度，不受力
    };

    // ============================================================
    // 形状类型
    // ============================================================
    enum class ShapeType : uint8 {
        Box,
        Circle,
        Edge,       ///< 线段（两个端点）
        Chain,      ///< 链状（多个顶点，首尾不相连）
        Polygon     ///< 自定义凸多边形
    };

    // ============================================================
    // 物理材质（摩擦系数 + 弹性系数）
    // ============================================================
    struct PhysicsMaterial {
        float32 friction    = 0.3f;   ///< 摩擦力系数 [0, 1]
        float32 restitution = 0.2f;   ///< 弹性系数 [0, 1]
        float32 density     = 1.0f;   ///< 密度 (kg/m²)，用于质量计算

        /// 组合两个材质（取几何平均常用于摩擦，取最大值常用于弹性）
        static PhysicsMaterial Combine(const PhysicsMaterial& a, const PhysicsMaterial& b) {
            PhysicsMaterial result;
            result.friction    = std::sqrt(a.friction * b.friction);
            result.restitution = (std::max)(a.restitution, b.restitution);
            result.density     = (a.density + b.density) * 0.5f;
            return result;
        }

        // 常用材质预设
        static PhysicsMaterial Stone()   { return {0.6f, 0.1f, 2.5f}; }
        static PhysicsMaterial Wood()    { return {0.4f, 0.2f, 0.8f}; }
        static PhysicsMaterial Metal()   { return {0.3f, 0.05f, 7.8f}; }
        static PhysicsMaterial Rubber()  { return {0.9f, 0.9f, 1.2f}; }
        static PhysicsMaterial Ice()     { return {0.03f, 0.0f, 0.9f}; }
        static PhysicsMaterial Bouncy()  { return {0.2f, 1.0f, 1.0f}; }
        static PhysicsMaterial Default() { return {0.3f, 0.2f, 1.0f}; }
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
        const Vec2* chainVertices   = nullptr;
        int32       chainVertexCount = 0;

        // Polygon: 凸多边形顶点列表
        const Vec2* polygonVertices   = nullptr;
        int32       polygonVertexCount = 0;

        // 相对于物体质心的偏移
        Vec2 offset = {0.0f, 0.0f};

        // 传感器标记（只触发碰撞回调，不产生物理碰撞）
        bool isSensor = false;

        // 材质（可单独覆盖，否则使用 BodyDef 中的材质）
        PhysicsMaterial material;
    };

    // ============================================================
    // Fixture 定义（运行时添加额外形状）
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
    // 力发生器抽象基类
    // ============================================================
    class IForceGenerator {
    public:
        virtual ~IForceGenerator() = default;

        /// @brief 应用力到刚体
        /// @param body 目标刚体
        /// @param dt 时间步长
        virtual void ApplyForce(IPhysicsBody* body, float32 dt) = 0;

        /// @brief 返回是否仍有效（可被外部移除）
        virtual bool IsValid() const { return true; }
    };

    /// 全局重力场
    struct GravityForce : public IForceGenerator {
        Vec2 gravity = {0.0f, -9.8f};  ///< 重力加速度 (m/s²)
        void ApplyForce(IPhysicsBody* body, float32 dt) override;
    };

    /// 线性空气阻力
    struct LinearDragForce : public IForceGenerator {
        float32 dragCoeff = 0.01f;  ///< 空气阻力系数
        void ApplyForce(IPhysicsBody* body, float32 dt) override;
    };

    // ============================================================
    // 刚体定义
    // ============================================================
    struct BodyDef {
        BodyType type = BodyType::Dynamic;

        // 初始位置和角度（世界坐标）
        Vec2   position = {0.0f, 0.0f};
        float32 angle   = 0.0f;  // 弧度

        // 初始速度
        Vec2   linearVelocity  = {0.0f, 0.0f};
        float32 angularVelocity = 0.0f;

        // 形状（创建时添加的第一个 Fixture）
        ShapeDef shape;

        // 物理材质（简化：直接设置）
        PhysicsMaterial material;

        // 阻尼
        float32 linearDamping  = 0.0f;
        float32 angularDamping = 0.0f;

        // 固定旋转（用于角色等不应旋转的物体）
        bool fixedRotation = false;

        // 允许休眠
        bool allowSleep = true;

        // 碰撞滤波
        uint16 categoryBits = 0x0001;
        uint16 maskBits     = 0xFFFF;
        int32  groupIndex   = 0;

        // 标记为 Bullet（启用 CCD）
        bool isBullet = false;

        // 用户数据
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
        Mouse,
        Spring   ///< 弹簧关节（新增）
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

        bool    enableLimit       = false;
        float32 lowerTranslation  = 0.0f;
        float32 upperTranslation  = 0.0f;
        bool    enableMotor       = false;
        float32 motorSpeed        = 0.0f;
        float32 maxMotorForce     = 0.0f;
    };

    /// 距离关节（绳子）
    struct DistanceJointDef : JointDef {
        DistanceJointDef() { type = JointType::Distance; }

        Vec2    localAnchorA  = {0.0f, 0.0f};
        Vec2    localAnchorB  = {0.0f, 0.0f};
        float32 length        = 1.0f;
        float32 stiffness     = 100.0f;
        float32 damping       = 1.0f;
    };

    /// 焊接关节（刚性连接）
    struct WeldJointDef : JointDef {
        WeldJointDef() { type = JointType::Weld; }

        Vec2    localAnchorA  = {0.0f, 0.0f};
        Vec2    localAnchorB  = {0.0f, 0.0f};
        float32 referenceAngle = 0.0f;
    };

    /// 车轮关节
    struct WheelJointDef : JointDef {
        WheelJointDef() { type = JointType::Wheel; }

        Vec2    localAnchorA   = {0.0f, 0.0f};
        Vec2    localAnchorB   = {0.0f, 0.0f};
        Vec2    localAxisA     = {0.0f, 1.0f};

        bool    enableMotor    = false;
        float32 motorSpeed     = 0.0f;
        float32 maxMotorTorque = 0.0f;

        float32 stiffness     = 100.0f;
        float32 damping       = 1.0f;
    };

    /// 鼠标关节
    struct MouseJointDef : JointDef {
        MouseJointDef() { type = JointType::Mouse; bodyB = nullptr; }

        Vec2          target     = {0.0f, 0.0f};
        float32       maxForce   = 1000.0f;
        float32       stiffness  = 100.0f;
        float32       damping    = 1.0f;
    };

    /// 弹簧关节
    struct SpringJointDef : JointDef {
        SpringJointDef() { type = JointType::Spring; }

        Vec2    localAnchorA  = {0.0f, 0.0f};
        Vec2    localAnchorB  = {0.0f, 0.0f};
        float32 restLength    = 1.0f;     ///< 自然长度
        float32 stiffness     = 50.0f;    ///< 弹性系数 k
        float32 damping       = 2.0f;     ///< 阻尼系数
    };

    // ============================================================
    // 接触流形（碰撞计算结果）
    // ============================================================
    /// 单个接触点
    struct ContactPoint {
        Vec2    point;         ///< 世界坐标接触点
        Vec2    normal;        ///< 从 A 指向 B 的法线（单位向量）
        float32 penetration;   ///< 穿透深度
        float32 normalImpulse;  ///< 法线方向冲量累积（用于 Warm Starting）
        float32 tangentImpulse; ///< 切线方向冲量累积
        bool    persisted;     ///< 是否为上一帧延续的接触点
    };

    /// 接触流形（两个形状之间的所有接触点）
    struct ContactManifold {
        IPhysicsBody* bodyA     = nullptr;
        IPhysicsBody* bodyB     = nullptr;

        ContactPoint  points[2];    ///< 最多 2 个接触点（2D SAT 通常 1-2 个）
        int32         pointCount   = 0;

        float32 friction    = 0.0f;
        float32 restitution = 0.0f;

        /// 清除接触点
        void Clear() { pointCount = 0; }

        /// 添加接触点
        void AddPoint(const Vec2& pt, const Vec2& nml, float32 pen) {
            if (pointCount < 2) {
                points[pointCount] = {pt, nml, pen, 0.0f, 0.0f, false};
                pointCount++;
            }
        }

        /// 从上一帧流形复制冲量（Warm Starting）
        void WarmStartFrom(const ContactManifold& prev) {
            for (int32 i = 0; i < pointCount && i < prev.pointCount; ++i) {
                // 找法线方向一致的接触点复用冲量
                float32 dot = Vec2::Dot(points[i].normal, prev.points[i].normal);
                if (dot > 0.9f) {
                    points[i].normalImpulse  = prev.points[i].normalImpulse;
                    points[i].tangentImpulse = prev.points[i].tangentImpulse;
                    points[i].persisted      = true;
                }
            }
        }
    };

    // ============================================================
    // 碰撞事件信息
    // ============================================================
    struct ContactInfo {
        const void* bodyA    = nullptr;
        const void* bodyB    = nullptr;
        Vec2        point;
        Vec2        normal;
        float32     impulse  = 0.0f;
    };

    struct ContactPersistData {
        const void* bodyA    = nullptr;
        const void* bodyB    = nullptr;
        Vec2        point;
        Vec2        normal;
        float32     impulse  = 0.0f;
        int32       pointCount = 0;
        float32     impulses[4] = {};
    };

    using ContactCallback        = std::function<void(const ContactInfo& info)>;
    using ContactPersistCallback = std::function<void(const ContactPersistData& data)>;
    using ContactFilterCallback  = std::function<bool(const void* bodyA,
                                                       const void* bodyB)>;

    // ============================================================
    // 碰撞滤波掩码工具
    // ============================================================
    enum CollisionLayers : uint16 {
        Layer_None     = 0,
        Layer_Default  = 0x0001,
        Layer_Player   = 0x0002,
        Layer_Enemy    = 0x0004,
        Layer_PlayerBullet = 0x0008,
        Layer_EnemyBullet  = 0x0010,
        Layer_Trigger  = 0x0020,
        Layer_All      = 0xFFFF
    };

    /// 创建碰撞滤波规则
    inline bool ShouldCollide(uint16 catA, uint16 maskA,
                              uint16 catB, uint16 maskB,
                              int32 groupA, int32 groupB) {
        if (groupA > 0 && groupA == groupB) return true;
        if (groupA < 0 && groupA == groupB) return false;
        return (catA & maskB) != 0 && (catB & maskA) != 0;
    }

    // ============================================================
    // 光线投射结果
    // ============================================================
    struct RayCastResult {
        const void* body     = nullptr;
        Vec2        point;
        Vec2        normal;
        float32     fraction = 1.0f;  ///< 0~1 沿射线的比例
    };

    // ============================================================
    // 形状投射结果（Shapecast）
    // ============================================================
    struct ShapeCastResult {
        IPhysicsBody* body     = nullptr;
        Vec2          point;       ///< 首次接触点
        Vec2          normal;      ///< 接触法线
        float32       fraction;    ///< 沿路径的比例 [0, 1]
    };

    // ============================================================
    // 物理世界配置
    // ============================================================
    struct PhysicsWorldConfig {
        Vec2    gravity              = {0.0f, -9.8f};
        int32   velocityIterations   = 8;    ///< 速度约束迭代次数
        int32   positionIterations   = 3;    ///< 位置修正迭代次数
        int32   solverIterations     = 10;   ///< 约束求解器迭代次数
        float32 fixedDt              = 1.0f / 60.0f;
        bool    enableSleep          = true;
        bool    enableCCD            = false;
        bool    useFixedDt           = true;
        bool    useMultithreading    = false; ///< 启用多线程物理管线（需外部调用 SetJobSystem 设置 JobSystem）
    };

} // namespace Engine