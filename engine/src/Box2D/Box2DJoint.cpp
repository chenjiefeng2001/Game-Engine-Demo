#include "Box2DJoint.h"
#include "Box2DPhysicsBody.h"
#include <box2d/box2d.h>
#include <cstdint>

namespace {

    b2Vec2 ToB2(const Engine::Vec2& v) {
        return b2Vec2(v.x, v.y);
    }

    Engine::Vec2 FromB2(const b2Vec2& v) {
        return Engine::Vec2(v.x, v.y);
    }

}

namespace Engine {

    // ============================================================
    // 工具函数
    // ============================================================

    static Vec2 FromB2(const b2Vec2& v) {
        return Vec2(v.x, v.y);
    }

    // ============================================================
    // 构造 / 析构
    // ============================================================

    Box2DJoint::Box2DJoint(b2Joint* joint, const JointDef& def)
        : m_Joint(joint)
        , m_Def(def)
        , m_UserData(def.userData)
    {
        if (m_Joint) {
            m_Joint->GetUserData().pointer = reinterpret_cast<uintptr_t>(this);
        }
    }

    Box2DJoint::~Box2DJoint() {
        m_Joint = nullptr;
    }

    // ============================================================
    // 类型安全向下转换
    // ============================================================

    b2RevoluteJoint* Box2DJoint::AsRevolute() const {
        return m_Joint && m_Joint->GetType() == e_revoluteJoint
            ? static_cast<b2RevoluteJoint*>(m_Joint) : nullptr;
    }

    b2PrismaticJoint* Box2DJoint::AsPrismatic() const {
        return m_Joint && m_Joint->GetType() == e_prismaticJoint
            ? static_cast<b2PrismaticJoint*>(m_Joint) : nullptr;
    }

    b2DistanceJoint* Box2DJoint::AsDistance() const {
        return m_Joint && m_Joint->GetType() == e_distanceJoint
            ? static_cast<b2DistanceJoint*>(m_Joint) : nullptr;
    }

    b2WeldJoint* Box2DJoint::AsWeld() const {
        return m_Joint && m_Joint->GetType() == e_weldJoint
            ? static_cast<b2WeldJoint*>(m_Joint) : nullptr;
    }

    b2WheelJoint* Box2DJoint::AsWheel() const {
        return m_Joint && m_Joint->GetType() == e_wheelJoint
            ? static_cast<b2WheelJoint*>(m_Joint) : nullptr;
    }

    b2MouseJoint* Box2DJoint::AsMouse() const {
        return m_Joint && m_Joint->GetType() == e_mouseJoint
            ? static_cast<b2MouseJoint*>(m_Joint) : nullptr;
    }

    // ============================================================
    // IJoint 接口
    // ============================================================

    JointType Box2DJoint::GetType() const {
        return m_Def.type;
    }

    IPhysicsBody* Box2DJoint::GetBodyA() const {
        if (!m_Joint) return nullptr;
        b2Body* bodyA = m_Joint->GetBodyA();
        return bodyA ? reinterpret_cast<IPhysicsBody*>(bodyA->GetUserData().pointer) : nullptr;
    }

    IPhysicsBody* Box2DJoint::GetBodyB() const {
        if (!m_Joint) return nullptr;
        b2Body* bodyB = m_Joint->GetBodyB();
        return bodyB ? reinterpret_cast<IPhysicsBody*>(bodyB->GetUserData().pointer) : nullptr;
    }

    Vec2 Box2DJoint::GetAnchorA() const {
        return m_Joint ? FromB2(m_Joint->GetAnchorA()) : Vec2(0.0f, 0.0f);
    }

    Vec2 Box2DJoint::GetAnchorB() const {
        return m_Joint ? FromB2(m_Joint->GetAnchorB()) : Vec2(0.0f, 0.0f);
    }

    bool Box2DJoint::IsEnabled() const {
        return m_Joint ? m_Joint->IsEnabled() : false;
    }

    bool Box2DJoint::GetCollideConnected() const {
        return m_Def.collideConnected;
    }

    void* Box2DJoint::GetUserData() const { return m_UserData; }
    void  Box2DJoint::SetUserData(void* data) { m_UserData = data; }

    // ============================================================
    // 特定关节类型方法
    // ============================================================

    float32 Box2DJoint::GetJointAngle() const {
        auto* j = AsRevolute();
        return j ? j->GetJointAngle() : 0.0f;
    }

    float32 Box2DJoint::GetJointSpeed() const {
        auto* j = AsRevolute();
        return j ? j->GetJointSpeed() : 0.0f;
    }

    float32 Box2DJoint::GetJointTranslation() const {
        auto* j = AsPrismatic();
        return j ? j->GetJointTranslation() : 0.0f;
    }

    float32 Box2DJoint::GetCurrentLength() const {
        auto* j = AsDistance();
        return j ? j->GetCurrentLength() : 0.0f;
    }

    // ── 电机 ──

    void Box2DJoint::SetMotorSpeed(float32 speed) {
        if (auto* r = AsRevolute()) r->SetMotorSpeed(speed);
        else if (auto* p = AsPrismatic()) p->SetMotorSpeed(speed);
        else if (auto* w = AsWheel()) w->SetMotorSpeed(speed);
    }

    float32 Box2DJoint::GetMotorSpeed() const {
        if (auto* r = AsRevolute()) return r->GetMotorSpeed();
        if (auto* p = AsPrismatic()) return p->GetMotorSpeed();
        if (auto* w = AsWheel()) return w->GetMotorSpeed();
        return 0.0f;
    }

    void Box2DJoint::SetMaxMotorTorque(float32 torque) {
        if (auto* r = AsRevolute()) r->SetMaxMotorTorque(torque);
    }

    float32 Box2DJoint::GetMotorTorque(float32 invDt) const {
        if (auto* r = AsRevolute()) return r->GetMotorTorque(invDt);
        return 0.0f;
    }

    // ── 限位 ──

    void Box2DJoint::EnableLimit(bool enable) {
        if (auto* r = AsRevolute()) r->EnableLimit(enable);
        else if (auto* p = AsPrismatic()) p->EnableLimit(enable);
    }

    void Box2DJoint::SetLimits(float32 lower, float32 upper) {
        if (auto* r = AsRevolute()) {
            r->SetLimits(lower, upper);
        } else if (auto* p = AsPrismatic()) {
            p->SetLimits(lower, upper);
        }
    }

    // ============================================================
    // MouseJoint 特有方法
    // ============================================================

    void Box2DJoint::UpdateMouseTarget(const Vec2& target) {
        if (auto* m = AsMouse()) {
            m->SetTarget(ToB2(target));
        }
    }

    void Box2DJoint::SetTarget(const Vec2& target) {
        if (auto* m = AsMouse()) m->SetTarget(ToB2(target));
    }

    Vec2 Box2DJoint::GetTarget() const {
        if (auto* m = AsMouse()) return FromB2(m->GetTarget());
        return Vec2(0.0f, 0.0f);
    }

    void Box2DJoint::SetMaxForce(float32 force) {
        if (auto* m = AsMouse()) m->SetMaxForce(force);
    }

    float32 Box2DJoint::GetMaxForce() const {
        if (auto* m = AsMouse()) return m->GetMaxForce();
        return 0.0f;
    }

    void Box2DJoint::SetStiffness(float32 stiffness) {
        if (auto* m = AsMouse()) m->SetStiffness(stiffness);
    }

    float32 Box2DJoint::GetStiffness() const {
        if (auto* m = AsMouse()) return m->GetStiffness();
        return 0.0f;
    }

    void Box2DJoint::SetDamping(float32 damping) {
        if (auto* m = AsMouse()) m->SetDamping(damping);
    }

    float32 Box2DJoint::GetDamping() const {
        if (auto* m = AsMouse()) return m->GetDamping();
        return 0.0f;
    }

    // ============================================================
    // 原生指针
    // ============================================================

    void* Box2DJoint::GetNativeJoint() {
        return static_cast<void*>(m_Joint);
    }

} // namespace Engine
