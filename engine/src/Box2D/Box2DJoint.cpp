#include "Box2DJoint.h"
#include "Box2DPhysicsBody.h"
#include <box2d/box2d.h>
#include <cstdint>

namespace Engine {

    // ============================================================
    // 工具函数
    // ============================================================

    static b2Vec2 ToB2(const Vec2& v) {
        return b2Vec2{v.x, v.y};
    }

    static Vec2 FromB2(const b2Vec2& v) {
        return Vec2(v.x, v.y);
    }

    // ── 根据引擎 JointType 获取 v3 b2JointType ──
    static bool IsRevoluteType(JointType type) {
        return type == JointType::Revolute;
    }
    static bool IsPrismaticType(JointType type) {
        return type == JointType::Prismatic;
    }
    static bool IsDistanceType(JointType type) {
        return type == JointType::Distance;
    }
    static bool IsWeldType(JointType type) {
        return type == JointType::Weld;
    }
    static bool IsWheelType(JointType type) {
        return type == JointType::Wheel;
    }
    static bool IsMouseType(JointType type) {
        return type == JointType::Mouse;
    }

    // ============================================================
    // 构造 / 析构
    // ============================================================

    Box2DJoint::Box2DJoint(b2JointId jointId, const JointDef& def)
        : m_JointId(jointId)
        , m_Def(def)
        , m_UserData(def.userData)
    {
        if (b2Joint_IsValid(m_JointId)) {
            b2Joint_SetUserData(m_JointId, this);
        }
    }

    Box2DJoint::~Box2DJoint() {
        m_JointId = {};
    }

    // ============================================================
    // IJoint 接口
    // ============================================================

    JointType Box2DJoint::GetType() const {
        return m_Def.type;
    }

    IPhysicsBody* Box2DJoint::GetBodyA() const {
        if (!b2Joint_IsValid(m_JointId)) return nullptr;
        b2BodyId bodyId = b2Joint_GetBodyA(m_JointId);
        return static_cast<IPhysicsBody*>(b2Body_GetUserData(bodyId));
    }

    IPhysicsBody* Box2DJoint::GetBodyB() const {
        if (!b2Joint_IsValid(m_JointId)) return nullptr;
        b2BodyId bodyId = b2Joint_GetBodyB(m_JointId);
        return static_cast<IPhysicsBody*>(b2Body_GetUserData(bodyId));
    }

    Vec2 Box2DJoint::GetAnchorA() const {
        // v3 没有直接获取 anchor 的函数，通过 local frame 近似
        if (!b2Joint_IsValid(m_JointId)) return Vec2(0.0f, 0.0f);
        b2Transform tf = b2Joint_GetLocalFrameA(m_JointId);
        return FromB2(tf.p);
    }

    Vec2 Box2DJoint::GetAnchorB() const {
        if (!b2Joint_IsValid(m_JointId)) return Vec2(0.0f, 0.0f);
        b2Transform tf = b2Joint_GetLocalFrameB(m_JointId);
        return FromB2(tf.p);
    }

    bool Box2DJoint::IsEnabled() const {
        return b2Joint_IsValid(m_JointId);
    }

    bool Box2DJoint::GetCollideConnected() const {
        return b2Joint_IsValid(m_JointId)
            ? b2Joint_GetCollideConnected(m_JointId) : m_Def.collideConnected;
    }

    void* Box2DJoint::GetUserData() const { return m_UserData; }
    void  Box2DJoint::SetUserData(void* data) { m_UserData = data; }

    // ============================================================
    // 特定关节类型方法（v3 使用带类型前缀的自由函数）
    // ============================================================

    float32 Box2DJoint::GetJointAngle() const {
        if (!b2Joint_IsValid(m_JointId)) return 0.0f;
        if (b2Joint_GetType(m_JointId) == b2_revoluteJoint)
            return b2RevoluteJoint_GetAngle(m_JointId);
        return 0.0f;
    }

    float32 Box2DJoint::GetJointSpeed() const {
        if (!b2Joint_IsValid(m_JointId)) return 0.0f;
        if (b2Joint_GetType(m_JointId) == b2_revoluteJoint) {
            b2BodyId bodyA = b2Joint_GetBodyA(m_JointId);
            b2BodyId bodyB = b2Joint_GetBodyB(m_JointId);
            return b2Body_GetAngularVelocity(bodyA) - b2Body_GetAngularVelocity(bodyB);
        }
        return 0.0f;
    }

    float32 Box2DJoint::GetJointTranslation() const {
        if (!b2Joint_IsValid(m_JointId)) return 0.0f;
        if (b2Joint_GetType(m_JointId) == b2_prismaticJoint)
            return b2PrismaticJoint_GetTranslation(m_JointId);
        return 0.0f;
    }

    float32 Box2DJoint::GetCurrentLength() const {
        if (!b2Joint_IsValid(m_JointId)) return 0.0f;
        if (b2Joint_GetType(m_JointId) == b2_distanceJoint)
            return b2DistanceJoint_GetCurrentLength(m_JointId);
        return 0.0f;
    }

    // ── 电机 ──

    void Box2DJoint::SetMotorSpeed(float32 speed) {
        if (!b2Joint_IsValid(m_JointId)) return;
        switch (b2Joint_GetType(m_JointId)) {
            case b2_revoluteJoint:
                b2RevoluteJoint_SetMotorSpeed(m_JointId, speed); break;
            case b2_prismaticJoint:
                b2PrismaticJoint_SetMotorSpeed(m_JointId, speed); break;
            case b2_wheelJoint:
                b2WheelJoint_SetMotorSpeed(m_JointId, speed); break;
            default: break;
        }
    }

    float32 Box2DJoint::GetMotorSpeed() const {
        if (!b2Joint_IsValid(m_JointId)) return 0.0f;
        switch (b2Joint_GetType(m_JointId)) {
            case b2_revoluteJoint: return b2RevoluteJoint_GetMotorSpeed(m_JointId);
            case b2_prismaticJoint: return b2PrismaticJoint_GetMotorSpeed(m_JointId);
            case b2_wheelJoint: return b2WheelJoint_GetMotorSpeed(m_JointId);
            default: return 0.0f;
        }
    }

    void Box2DJoint::SetMaxMotorTorque(float32 torque) {
        if (!b2Joint_IsValid(m_JointId)) return;
        if (b2Joint_GetType(m_JointId) == b2_revoluteJoint)
            b2RevoluteJoint_SetMaxMotorTorque(m_JointId, torque);
    }

    float32 Box2DJoint::GetMotorTorque(float32 invDt) const {
        if (!b2Joint_IsValid(m_JointId)) return 0.0f;
        if (b2Joint_GetType(m_JointId) == b2_revoluteJoint)
            return b2RevoluteJoint_GetMotorTorque(m_JointId);
        return 0.0f;
    }

    // ── 限位 ──

    void Box2DJoint::EnableLimit(bool enable) {
        if (!b2Joint_IsValid(m_JointId)) return;
        switch (b2Joint_GetType(m_JointId)) {
            case b2_revoluteJoint:
                b2RevoluteJoint_EnableLimit(m_JointId, enable); break;
            case b2_prismaticJoint:
                b2PrismaticJoint_EnableLimit(m_JointId, enable); break;
            default: break;
        }
    }

    void Box2DJoint::SetLimits(float32 lower, float32 upper) {
        if (!b2Joint_IsValid(m_JointId)) return;
        switch (b2Joint_GetType(m_JointId)) {
            case b2_revoluteJoint:
                b2RevoluteJoint_SetLimits(m_JointId, lower, upper); break;
            case b2_prismaticJoint:
                b2PrismaticJoint_SetLimits(m_JointId, lower, upper); break;
            default: break;
        }
    }

    // ============================================================
    // MouseJoint 替代实现（Box2D 3.x 使用 MotorJoint）
    // ============================================================

    void Box2DJoint::UpdateMouseTarget(const Vec2& target) {
        if (!b2Joint_IsValid(m_JointId)) return;
        if (m_Def.type != JointType::Mouse) return;

        // 移动 bodyA（静态锚点）到鼠标目标位置
        b2BodyId bodyA = b2Joint_GetBodyA(m_JointId);
        b2BodyId bodyB = b2Joint_GetBodyB(m_JointId);
        if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB)) return;

        b2Body_SetTransform(bodyA, ToB2(target), b2Body_GetRotation(bodyA));
        // 唤醒被拖拽体，使其响应弹簧力
        b2Body_SetAwake(bodyB, true);
    }

    void Box2DJoint::SetTarget(const Vec2& target) {
        UpdateMouseTarget(target);
    }

    Vec2 Box2DJoint::GetTarget() const {
        if (!b2Joint_IsValid(m_JointId)) return Vec2(0.0f, 0.0f);
        if (m_Def.type != JointType::Mouse) return Vec2(0.0f, 0.0f);
        b2BodyId bodyA = b2Joint_GetBodyA(m_JointId);
        if (!b2Body_IsValid(bodyA)) return Vec2(0.0f, 0.0f);
        return FromB2(b2Body_GetPosition(bodyA));
    }

    void Box2DJoint::SetMaxForce(float32 force) {
        if (!b2Joint_IsValid(m_JointId)) return;
        if (b2Joint_GetType(m_JointId) == b2_motorJoint)
            b2MotorJoint_SetMaxSpringForce(m_JointId, force);
    }

    float32 Box2DJoint::GetMaxForce() const {
        if (!b2Joint_IsValid(m_JointId)) return 0.0f;
        if (b2Joint_GetType(m_JointId) == b2_motorJoint)
            return b2MotorJoint_GetMaxSpringForce(m_JointId);
        return 0.0f;
    }

    void Box2DJoint::SetStiffness(float32 stiffness) {
        if (!b2Joint_IsValid(m_JointId)) return;
        if (b2Joint_GetType(m_JointId) == b2_motorJoint)
            b2MotorJoint_SetLinearHertz(m_JointId, stiffness);
    }

    float32 Box2DJoint::GetStiffness() const {
        if (!b2Joint_IsValid(m_JointId)) return 0.0f;
        if (b2Joint_GetType(m_JointId) == b2_motorJoint)
            return b2MotorJoint_GetLinearHertz(m_JointId);
        return 0.0f;
    }

    void Box2DJoint::SetDamping(float32 damping) {
        if (!b2Joint_IsValid(m_JointId)) return;
        if (b2Joint_GetType(m_JointId) == b2_motorJoint)
            b2MotorJoint_SetLinearDampingRatio(m_JointId, damping);
    }

    float32 Box2DJoint::GetDamping() const {
        if (!b2Joint_IsValid(m_JointId)) return 0.0f;
        if (b2Joint_GetType(m_JointId) == b2_motorJoint)
            return b2MotorJoint_GetLinearDampingRatio(m_JointId);
        return 0.0f;
    }

    // ============================================================
    // 原生指针（返回 b2JointId*）
    // ============================================================

    void* Box2DJoint::GetNativeJoint() {
        return static_cast<void*>(&m_JointId);
    }

} // namespace Engine
