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

    Box2DPhysicsBody::Box2DPhysicsBody(b2BodyId bodyId, const BodyDef& def)
        : m_BodyId(bodyId)
        , m_UserData(def.userData)
    {
        if (b2Body_IsValid(m_BodyId)) {
            b2Body_SetUserData(m_BodyId, this);
        }
    }

    Box2DPhysicsBody::~Box2DPhysicsBody() {
        // body 由 Box2DPhysicsWorld 的 DestroyBody 释放
        m_BodyId = {};
    }

    // ============================================================
    // 变换
    // ============================================================

    void Box2DPhysicsBody::SetTransform(const Vec2& position, float32 angle) {
        if (b2Body_IsValid(m_BodyId)) {
            b2Body_SetTransform(m_BodyId, ToB2(position), b2MakeRot(angle));
        }
    }

    Vec2 Box2DPhysicsBody::GetPosition() const {
        if (!b2Body_IsValid(m_BodyId)) return Vec2(0.0f, 0.0f);
        return FromB2(b2Body_GetPosition(m_BodyId));
    }

    float32 Box2DPhysicsBody::GetAngle() const {
        if (!b2Body_IsValid(m_BodyId)) return 0.0f;
        return b2Rot_GetAngle(b2Body_GetRotation(m_BodyId));
    }

    // ============================================================
    // 速度
    // ============================================================

    void Box2DPhysicsBody::SetLinearVelocity(const Vec2& velocity) {
        if (b2Body_IsValid(m_BodyId))
            b2Body_SetLinearVelocity(m_BodyId, ToB2(velocity));
    }

    Vec2 Box2DPhysicsBody::GetLinearVelocity() const {
        if (!b2Body_IsValid(m_BodyId)) return Vec2(0.0f, 0.0f);
        return FromB2(b2Body_GetLinearVelocity(m_BodyId));
    }

    void Box2DPhysicsBody::SetAngularVelocity(float32 omega) {
        if (b2Body_IsValid(m_BodyId))
            b2Body_SetAngularVelocity(m_BodyId, omega);
    }

    float32 Box2DPhysicsBody::GetAngularVelocity() const {
        return b2Body_IsValid(m_BodyId) ? b2Body_GetAngularVelocity(m_BodyId) : 0.0f;
    }

    // ============================================================
    // 力 / 冲量
    // ============================================================

    void Box2DPhysicsBody::ApplyForce(const Vec2& force, const Vec2& point) {
        if (b2Body_IsValid(m_BodyId))
            b2Body_ApplyForce(m_BodyId, ToB2(force), ToB2(point), true);
    }

    void Box2DPhysicsBody::ApplyForceToCenter(const Vec2& force) {
        if (b2Body_IsValid(m_BodyId))
            b2Body_ApplyForceToCenter(m_BodyId, ToB2(force), true);
    }

    void Box2DPhysicsBody::ApplyLinearImpulse(const Vec2& impulse, const Vec2& point) {
        if (b2Body_IsValid(m_BodyId))
            b2Body_ApplyLinearImpulse(m_BodyId, ToB2(impulse), ToB2(point), true);
    }

    // ============================================================
    // 属性
    // ============================================================

    void Box2DPhysicsBody::SetType(BodyType type) {
        if (b2Body_IsValid(m_BodyId))
            b2Body_SetType(m_BodyId, ToB2Type(type));
    }

    BodyType Box2DPhysicsBody::GetType() const {
        return b2Body_IsValid(m_BodyId)
            ? FromB2Type(b2Body_GetType(m_BodyId)) : BodyType::Static;
    }

    float32 Box2DPhysicsBody::GetMass() const {
        return b2Body_IsValid(m_BodyId) ? b2Body_GetMass(m_BodyId) : 0.0f;
    }

    float32 Box2DPhysicsBody::GetInertia() const {
        return b2Body_IsValid(m_BodyId) ? b2Body_GetRotationalInertia(m_BodyId) : 0.0f;
    }

    // ============================================================
    // 阻尼
    // ============================================================

    void Box2DPhysicsBody::SetLinearDamping(float32 damping) {
        if (b2Body_IsValid(m_BodyId))
            b2Body_SetLinearDamping(m_BodyId, damping);
    }

    float32 Box2DPhysicsBody::GetLinearDamping() const {
        return b2Body_IsValid(m_BodyId) ? b2Body_GetLinearDamping(m_BodyId) : 0.0f;
    }

    void Box2DPhysicsBody::SetAngularDamping(float32 damping) {
        if (b2Body_IsValid(m_BodyId))
            b2Body_SetAngularDamping(m_BodyId, damping);
    }

    float32 Box2DPhysicsBody::GetAngularDamping() const {
        return b2Body_IsValid(m_BodyId) ? b2Body_GetAngularDamping(m_BodyId) : 0.0f;
    }

    // ============================================================
    // Shape 管理（v3 代替 v2 的 Fixture）
    // ============================================================

    /// 在刚体上创建形状（内部由 AddFixture / 构造调用）
    b2ShapeId Box2DPhysicsBody::CreateShapeFromDef(const FixtureDef& def) {
        if (!b2Body_IsValid(m_BodyId)) return b2ShapeId{};

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density     = def.density;
        shapeDef.material.friction    = def.friction;
        shapeDef.material.restitution = def.restitution;
        shapeDef.isSensor    = def.isSensor;

        b2Filter filter = b2DefaultFilter();
        filter.categoryBits = static_cast<uint64_t>(def.categoryBits);
        filter.maskBits     = static_cast<uint64_t>(def.maskBits);
        filter.groupIndex   = def.groupIndex;
        shapeDef.filter = filter;
        shapeDef.userData = this;

        b2ShapeId shapeId = {};

        switch (def.shape.type) {
            case ShapeType::Box: {
                b2Polygon polygon = b2MakeOffsetBox(
                    def.shape.boxSize.x, def.shape.boxSize.y,
                    ToB2(def.shape.offset), b2MakeRot(0.0f));
                shapeId = b2CreatePolygonShape(m_BodyId, &shapeDef, &polygon);
                break;
            }
            case ShapeType::Circle: {
                b2Circle circle;
                circle.center = ToB2(def.shape.offset);
                circle.radius = def.shape.circleRadius;
                shapeId = b2CreateCircleShape(m_BodyId, &shapeDef, &circle);
                break;
            }
            case ShapeType::Edge: {
                b2Segment segment;
                segment.point1 = ToB2(def.shape.edgeStart);
                segment.point2 = ToB2(def.shape.edgeEnd);
                shapeId = b2CreateSegmentShape(m_BodyId, &shapeDef, &segment);
                break;
            }
            case ShapeType::Chain: {
                if (def.shape.chainVertices && def.shape.chainVertexCount >= 2) {
                    // v3 链形状：逐段创建 segment
                    for (int32 i = 0; i < def.shape.chainVertexCount - 1; ++i) {
                        b2Segment seg;
                        seg.point1 = ToB2(def.shape.chainVertices[i]);
                        seg.point2 = ToB2(def.shape.chainVertices[i + 1]);
                        b2CreateSegmentShape(m_BodyId, &shapeDef, &seg);
                    }
                }
                return b2ShapeId{};  // 链由多个 segment 组成，返回空 ID
            }
        }

        return shapeId;
    }

    void* Box2DPhysicsBody::AddFixture(const FixtureDef& def) {
        b2ShapeId shapeId = CreateShapeFromDef(def);
        if (!b2Shape_IsValid(shapeId) && def.shape.type != ShapeType::Chain) {
            return nullptr;
        }

        ShapeEntry entry;
        entry.shapeId  = shapeId;
        entry.userData = def.userData;
        m_Shapes.push_back(entry);

        // 返回指向 ShapeEntry 的指针（作为不透明句柄）
        return static_cast<void*>(&m_Shapes.back());
    }

    void Box2DPhysicsBody::RemoveFixture(void* fixtureId) {
        if (!fixtureId) return;

        for (auto it = m_Shapes.begin(); it != m_Shapes.end(); ++it) {
            if (static_cast<void*>(&(*it)) == fixtureId) {
                if (b2Shape_IsValid(it->shapeId)) {
                    b2DestroyShape(it->shapeId, true);
                }
                m_Shapes.erase(it);
                return;
            }
        }
    }

    void Box2DPhysicsBody::ClearFixtures() {
        for (auto& entry : m_Shapes) {
            if (b2Shape_IsValid(entry.shapeId)) {
                b2DestroyShape(entry.shapeId, true);
            }
        }
        m_Shapes.clear();
    }

    // ============================================================
    // 碰撞滤波（v3 不支持运行时修改，简化实现）
    // ============================================================

    void Box2DPhysicsBody::SetFilterData(uint16 categoryBits, uint16 maskBits) {
        (void)categoryBits;
        (void)maskBits;
    }

    void Box2DPhysicsBody::SetGroupIndex(int32 groupIndex) {

        (void)groupIndex;
    }

    // ============================================================
    // 激活
    // ============================================================

    void Box2DPhysicsBody::SetActive(bool active) {
        if (b2Body_IsValid(m_BodyId)) {
            if (active) b2Body_Enable(m_BodyId);
            else b2Body_Disable(m_BodyId);
        }
    }

    bool Box2DPhysicsBody::IsActive() const {
        return b2Body_IsValid(m_BodyId) ? b2Body_IsEnabled(m_BodyId) : false;
    }

    // ============================================================
    // 用户数据
    // ============================================================

    void* Box2DPhysicsBody::GetUserData() const {
        return m_UserData;
    }

    void Box2DPhysicsBody::SetUserData(void* data) {
        m_UserData = data;
        if (b2Body_IsValid(m_BodyId)) {
            b2Body_SetUserData(m_BodyId, data);
        }
    }

    // ============================================================
    // 休眠
    // ============================================================

    void Box2DPhysicsBody::SetAwake(bool awake) {
        if (b2Body_IsValid(m_BodyId))
            b2Body_SetAwake(m_BodyId, awake);
    }

    bool Box2DPhysicsBody::IsAwake() const {
        return b2Body_IsValid(m_BodyId) ? b2Body_IsAwake(m_BodyId) : false;
    }

    // ============================================================
    // 原生指针（返回 b2BodyId* 的 void*）
    // ============================================================

    void* Box2DPhysicsBody::GetNativeBody() {
        return static_cast<void*>(&m_BodyId);
    }

} // namespace Engine
