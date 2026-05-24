#include "Box2DPhysicsBody.h"
#include <box2d/box2d.h>
#include <cstring>

namespace Engine {

    // ============================================================
    // 工具函数：引擎类型 ↔ Box2D 类型转换
    // ============================================================

    static b2Vec2 ToB2(const Vec2& v) {
        return b2Vec2(v.x, v.y);
    }

    static Vec2 FromB2(const b2Vec2& v) {
        return Vec2(v.x, v.y);
    }

    static b2BodyType ToB2Type(BodyType type) {
        switch (type) {
            case BodyType::Static:    return b2_staticBody;
            case BodyType::Dynamic:   return b2_dynamicBody;
            case BodyType::Kinematic: return b2_kinematicBody;
        }
        return b2_staticBody;
    }

    static BodyType FromB2Type(b2BodyType type) {
        switch (type) {
            case b2_staticBody:    return BodyType::Static;
            case b2_dynamicBody:   return BodyType::Dynamic;
            case b2_kinematicBody: return BodyType::Kinematic;
        }
        return BodyType::Static;
    }

    // ============================================================
    // 构造 / 析构
    // ============================================================

    Box2DPhysicsBody::Box2DPhysicsBody(b2Body* body, const BodyDef& def)
        : m_Body(body)
        , m_UserData(def.userData)
    {
        if (m_Body) {
            // Box2D 2.4.1 的 SetUserData 声明了但未实现，
            // 通过 GetUserData() 返回的引用直接设置 .pointer。
            m_Body->GetUserData().pointer = reinterpret_cast<uintptr_t>(this);
        }
    }

    Box2DPhysicsBody::~Box2DPhysicsBody() {
        // body 由 Box2DPhysicsWorld 的 DestroyBody 释放
        m_Body = nullptr;
    }

    // ============================================================
    // 变换
    // ============================================================

    void Box2DPhysicsBody::SetTransform(const Vec2& position, float32 angle) {
        if (m_Body) m_Body->SetTransform(ToB2(position), angle);
    }

    Vec2 Box2DPhysicsBody::GetPosition() const {
        if (!m_Body) return Vec2(0.0f, 0.0f);
        return FromB2(m_Body->GetPosition());
    }

    float32 Box2DPhysicsBody::GetAngle() const {
        return m_Body ? m_Body->GetAngle() : 0.0f;
    }

    // ============================================================
    // 速度
    // ============================================================

    void Box2DPhysicsBody::SetLinearVelocity(const Vec2& velocity) {
        if (m_Body) m_Body->SetLinearVelocity(ToB2(velocity));
    }

    Vec2 Box2DPhysicsBody::GetLinearVelocity() const {
        if (!m_Body) return Vec2(0.0f, 0.0f);
        return FromB2(m_Body->GetLinearVelocity());
    }

    void Box2DPhysicsBody::SetAngularVelocity(float32 omega) {
        if (m_Body) m_Body->SetAngularVelocity(omega);
    }

    float32 Box2DPhysicsBody::GetAngularVelocity() const {
        return m_Body ? m_Body->GetAngularVelocity() : 0.0f;
    }

    // ============================================================
    // 力 / 冲量
    // ============================================================

    void Box2DPhysicsBody::ApplyForce(const Vec2& force, const Vec2& point) {
        if (m_Body) m_Body->ApplyForce(ToB2(force), ToB2(point), true);
    }

    void Box2DPhysicsBody::ApplyForceToCenter(const Vec2& force) {
        if (m_Body) m_Body->ApplyForceToCenter(ToB2(force), true);
    }

    void Box2DPhysicsBody::ApplyLinearImpulse(const Vec2& impulse, const Vec2& point) {
        if (m_Body) m_Body->ApplyLinearImpulse(ToB2(impulse), ToB2(point), true);
    }

    // ============================================================
    // 属性
    // ============================================================

    void Box2DPhysicsBody::SetType(BodyType type) {
        if (m_Body) m_Body->SetType(ToB2Type(type));
    }

    BodyType Box2DPhysicsBody::GetType() const {
        return m_Body ? FromB2Type(m_Body->GetType()) : BodyType::Static;
    }

    float32 Box2DPhysicsBody::GetMass() const {
        return m_Body ? m_Body->GetMass() : 0.0f;
    }

    float32 Box2DPhysicsBody::GetInertia() const {
        return m_Body ? m_Body->GetInertia() : 0.0f;
    }

    // ============================================================
    // 阻尼
    // ============================================================

    void Box2DPhysicsBody::SetLinearDamping(float32 damping) {
        if (m_Body) m_Body->SetLinearDamping(damping);
    }

    float32 Box2DPhysicsBody::GetLinearDamping() const {
        return m_Body ? m_Body->GetLinearDamping() : 0.0f;
    }

    void Box2DPhysicsBody::SetAngularDamping(float32 damping) {
        if (m_Body) m_Body->SetAngularDamping(damping);
    }

    float32 Box2DPhysicsBody::GetAngularDamping() const {
        return m_Body ? m_Body->GetAngularDamping() : 0.0f;
    }

    // ============================================================
    // Fixture 管理
    // ============================================================

    b2FixtureDef Box2DPhysicsBody::ToB2FixtureDef(const FixtureDef& def) {
        b2FixtureDef fd;
        fd.density     = def.density;
        fd.friction    = def.friction;
        fd.restitution = def.restitution;
        fd.isSensor    = def.isSensor;

        b2Filter filter;
        filter.categoryBits = def.categoryBits;
        filter.maskBits     = def.maskBits;
        filter.groupIndex   = def.groupIndex;
        fd.filter = filter;

        // 形状
        b2PolygonShape   boxShape;
        b2CircleShape    circleShape;
        b2EdgeShape      edgeShape;
        b2ChainShape     chainShape;

        switch (def.shape.type) {
            case ShapeType::Box:
                boxShape.SetAsBox(def.shape.boxSize.x, def.shape.boxSize.y,
                                  ToB2(def.shape.offset), 0.0f);
                fd.shape = &boxShape;
                break;
            case ShapeType::Circle:
                circleShape.m_radius = def.shape.circleRadius;
                circleShape.m_p      = ToB2(def.shape.offset);
                fd.shape = &circleShape;
                break;
            case ShapeType::Edge:
                edgeShape.SetTwoSided(ToB2(def.shape.edgeStart),
                                      ToB2(def.shape.edgeEnd));
                fd.shape = &edgeShape;
                break;
            case ShapeType::Chain:
                if (def.shape.chainVertices && def.shape.chainVertexCount > 0) {
                    // 需要 prev/next 顶点来闭合链
                    b2Vec2 prev = ToB2(def.shape.chainVertices[0]);
                    b2Vec2 next = ToB2(def.shape.chainVertices[def.shape.chainVertexCount - 1]);
                    chainShape.CreateChain(
                        reinterpret_cast<const b2Vec2*>(def.shape.chainVertices),
                        def.shape.chainVertexCount, prev, next);
                    fd.shape = &chainShape;
                }
                break;
        }

        return fd;
    }

    void* Box2DPhysicsBody::AddFixture(const FixtureDef& def) {
        if (!m_Body) return nullptr;

        b2FixtureDef fd = ToB2FixtureDef(def);
        b2Fixture* fixture = m_Body->CreateFixture(&fd);
        if (!fixture) return nullptr;

        FixtureEntry entry;
        entry.fixture  = fixture;
        entry.userData = def.userData;
        fixture->GetUserData().pointer = reinterpret_cast<uintptr_t>(this);

        m_Fixtures.push_back(entry);
        return static_cast<void*>(fixture);
    }

    void Box2DPhysicsBody::RemoveFixture(void* fixtureId) {
        if (!m_Body || !fixtureId) return;

        b2Fixture* target = static_cast<b2Fixture*>(fixtureId);
        for (auto it = m_Fixtures.begin(); it != m_Fixtures.end(); ++it) {
            if (it->fixture == target) {
                m_Body->DestroyFixture(target);
                m_Fixtures.erase(it);
                return;
            }
        }
    }

    void Box2DPhysicsBody::ClearFixtures() {
        if (!m_Body) return;

        for (auto& entry : m_Fixtures) {
            if (entry.fixture) {
                m_Body->DestroyFixture(entry.fixture);
            }
        }
        m_Fixtures.clear();
    }

    // ============================================================
    // 碰撞滤波
    // ============================================================

    void Box2DPhysicsBody::SetFilterData(uint16 categoryBits, uint16 maskBits) {
        if (!m_Body) return;

        b2Filter filter;
        filter.categoryBits = categoryBits;
        filter.maskBits     = maskBits;
        filter.groupIndex   = 0;

        for (b2Fixture* f = m_Body->GetFixtureList(); f; f = f->GetNext()) {
            f->SetFilterData(filter);
        }
    }

    void Box2DPhysicsBody::SetGroupIndex(int32 groupIndex) {
        if (!m_Body) return;

        for (b2Fixture* f = m_Body->GetFixtureList(); f; f = f->GetNext()) {
            b2Filter filter = f->GetFilterData();
            filter.groupIndex = groupIndex;
            f->SetFilterData(filter);
        }
    }

    // ============================================================
    // 激活
    // ============================================================

    void Box2DPhysicsBody::SetActive(bool active) {
        if (m_Body) m_Body->SetEnabled(active);
    }

    bool Box2DPhysicsBody::IsActive() const {
        return m_Body ? m_Body->IsEnabled() : false;
    }

    // ============================================================
    // 用户数据
    // ============================================================

    void* Box2DPhysicsBody::GetUserData() const {
        return m_UserData;
    }

    void Box2DPhysicsBody::SetUserData(void* data) {
        m_UserData = data;
        // 同时同步到 Box2D body
        if (m_Body) {
            m_Body->GetUserData().pointer = reinterpret_cast<uintptr_t>(data);
        }
    }

    // ============================================================
    // 休眠
    // ============================================================

    void Box2DPhysicsBody::SetAwake(bool awake) {
        if (m_Body) m_Body->SetAwake(awake);
    }

    bool Box2DPhysicsBody::IsAwake() const {
        return m_Body ? m_Body->IsAwake() : false;
    }

    // ============================================================
    // 原生指针
    // ============================================================

    void* Box2DPhysicsBody::GetNativeBody() {
        return static_cast<void*>(m_Body);
    }

} // namespace Engine
