#include "Engine/Core/Physics/Bare2DPhysicsBody.h"
#include <cmath>
#include <algorithm>

namespace Engine {

    void BarePolygon::Set(const Vec2* verts, int32 count) {
        vertices.resize(count);
        normals.resize(count);
        vertexCount = count;
        for (int32 i = 0; i < count; ++i)
            vertices[i] = verts[i];
        BuildNormals();
    }

    void BarePolygon::BuildNormals() {
        for (int32 i = 0; i < vertexCount; ++i) {
            int32 iNext = (i + 1) % vertexCount;
            Vec2 edge = vertices[iNext] - vertices[i];
            normals[i] = Vec2(edge.y, -edge.x);
            float32 len = std::sqrt(normals[i].x * normals[i].x + normals[i].y * normals[i].y);
            if (len > 1e-8f) {
                normals[i].x /= len;
                normals[i].y /= len;
            }
        }
    }

    void BarePolygon::ProjectOntoAxis(const Vec2& axis, const Vec2& pos, float32 angle,
                                       float32& outMin, float32& outMax) const {
        outMin = 1e30f;
        outMax = -1e30f;
        float32 c = std::cos(angle), s = std::sin(angle);
        for (int32 i = 0; i < vertexCount; ++i) {
            float32 wx = vertices[i].x * c - vertices[i].y * s + pos.x;
            float32 wy = vertices[i].x * s + vertices[i].y * c + pos.y;
            float32 proj = wx * axis.x + wy * axis.y;
            if (proj < outMin) outMin = proj;
            if (proj > outMax) outMax = proj;
        }
    }

    Bare2DPhysicsBody::Bare2DPhysicsBody(const BodyDef& def)
        : m_Position(def.position)
        , m_Angle(def.angle)
        , m_LinearVelocity(def.linearVelocity)
        , m_AngularVelocity(def.angularVelocity)
        , m_Type(def.type)
        , m_LinearDamping(def.linearDamping)
        , m_AngularDamping(def.angularDamping)
        , m_Material(def.material)
        , m_FixedRotation(def.fixedRotation)
        , m_AllowSleep(def.allowSleep)
        , m_IsBullet(def.isBullet)
        , m_UserData(def.userData)
        , m_CategoryBits(def.categoryBits)
        , m_MaskBits(def.maskBits)
        , m_GroupIndex(def.groupIndex)
        , m_DefaultShape(def.shape)
    {
        // 添加初始 Fixture
        InternalFixture fix;
        fix.shape = def.shape;
        fix.material = def.material;
        fix.isSensor = def.shape.isSensor;
        fix.categoryBits = def.categoryBits;
        fix.maskBits = def.maskBits;
        fix.groupIndex = def.groupIndex;

        if (def.shape.type == ShapeType::Polygon && def.shape.polygonVertices && def.shape.polygonVertexCount > 0) {
            fix.polygon.Set(def.shape.polygonVertices, def.shape.polygonVertexCount);
        }
        m_Fixtures.push_back(std::move(fix));

        UpdateAABB();
        RecalculateMass();

        // 默认添加重力力发生器初始化
        m_PrevPosition = m_Position;
        m_PrevAngle = m_Angle;
    }

    void Bare2DPhysicsBody::SetTransform(const Vec2& position, float32 angle) {
        m_Position = position;
        m_Angle = angle;
        UpdateAABB();
    }

    void Bare2DPhysicsBody::ApplyForce(const Vec2& force, const Vec2& point) {
        if (m_Type != BodyType::Dynamic || !m_Awake) return;
        m_ForceAccum += force;
        Vec2 r = point - m_Position;
        m_TorqueAccum += Vec2::Cross(r, force);
    }

    void Bare2DPhysicsBody::ApplyForceToCenter(const Vec2& force) {
        if (m_Type != BodyType::Dynamic || !m_Awake) return;
        m_ForceAccum += force;
    }

    void Bare2DPhysicsBody::ApplyLinearImpulse(const Vec2& impulse, const Vec2& point) {
        if (m_Type != BodyType::Dynamic || !m_Awake) return;
        m_LinearVelocity += impulse * m_InvMass;
        Vec2 r = point - m_Position;
        m_AngularVelocity += m_InvInertia * Vec2::Cross(r, impulse);
    }

    void Bare2DPhysicsBody::SetType(BodyType type) {
        m_Type = type;
        if (type != BodyType::Dynamic) {
            m_LinearVelocity = Vec2(0, 0);
            m_AngularVelocity = 0.0f;
            m_Awake = false;
        }
        RecalculateMass();
    }

    void* Bare2DPhysicsBody::AddFixture(const FixtureDef& def) {
        InternalFixture fix;
        fix.shape = def.shape;
        fix.material.friction = def.friction;
        fix.material.restitution = def.restitution;
        fix.material.density = def.density;
        fix.isSensor = def.isSensor;
        fix.categoryBits = def.categoryBits;
        fix.maskBits = def.maskBits;
        fix.groupIndex = def.groupIndex;
        fix.userData = def.userData;

        if (def.shape.type == ShapeType::Polygon && def.shape.polygonVertices && def.shape.polygonVertexCount > 0) {
            fix.polygon.Set(def.shape.polygonVertices, def.shape.polygonVertexCount);
        }

        ComputeAABBForFixture(fix);
        m_Fixtures.push_back(std::move(fix));
        RecalculateMass();

        return reinterpret_cast<void*>(static_cast<intptr_t>(m_Fixtures.size() - 1));
    }

    void Bare2DPhysicsBody::RemoveFixture(void* fixtureId) {
        int32 index = static_cast<int32>(reinterpret_cast<intptr_t>(fixtureId));
        if (index >= 0 && index < static_cast<int32>(m_Fixtures.size())) {
            m_Fixtures.erase(m_Fixtures.begin() + index);
            RecalculateMass();
        }
    }

    void Bare2DPhysicsBody::ClearFixtures() {
        m_Fixtures.clear();
        RecalculateMass();
    }

    void Bare2DPhysicsBody::SetFilterData(uint16 categoryBits, uint16 maskBits) {
        m_CategoryBits = categoryBits;
        m_MaskBits = maskBits;
        for (auto& fix : m_Fixtures) {
            fix.categoryBits = categoryBits;
            fix.maskBits = maskBits;
        }
    }

    void Bare2DPhysicsBody::SetGroupIndex(int32 groupIndex) {
        m_GroupIndex = groupIndex;
        for (auto& fix : m_Fixtures) {
            fix.groupIndex = groupIndex;
        }
    }

    void Bare2DPhysicsBody::SetAwake(bool awake) {
        if (m_Type != BodyType::Dynamic) return;
        if (awake) {
            m_Awake = true;
            m_SleepTimer = 0.0f;
        } else {
            m_Awake = false;
            m_LinearVelocity = Vec2(0, 0);
            m_AngularVelocity = 0.0f;
        }
    }

    void Bare2DPhysicsBody::UpdateAABB() {
        for (auto& fix : m_Fixtures) {
            ComputeAABBForFixture(fix);
        }
    }

    void Bare2DPhysicsBody::ComputeAABBForFixture(InternalFixture& fix) {
        switch (fix.shape.type) {
            case ShapeType::Circle: {
                float32 r = fix.shape.circleRadius;
                fix.aabbMin = Vec2(m_Position.x - r, m_Position.y - r);
                fix.aabbMax = Vec2(m_Position.x + r, m_Position.y + r);
                break;
            }
            case ShapeType::Box: {
                float32 c = std::cos(m_Angle), s = std::sin(m_Angle);
                Vec2 h = fix.shape.boxSize;
                // 旋转后的 AABB（保守包围盒）
                float32 rx = std::abs(h.x * c) + std::abs(h.y * s);
                float32 ry = std::abs(h.x * s) + std::abs(h.y * c);
                fix.aabbMin = Vec2(m_Position.x - rx, m_Position.y - ry);
                fix.aabbMax = Vec2(m_Position.x + rx, m_Position.y + ry);
                break;
            }
            case ShapeType::Polygon: {
                const auto& poly = fix.polygon;
                float32 minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
                float32 c = std::cos(m_Angle), s = std::sin(m_Angle);
                for (int32 i = 0; i < poly.vertexCount; ++i) {
                    float32 wx = poly.vertices[i].x * c - poly.vertices[i].y * s + m_Position.x;
                    float32 wy = poly.vertices[i].x * s + poly.vertices[i].y * c + m_Position.y;
                    if (wx < minX) minX = wx; if (wx > maxX) maxX = wx;
                    if (wy < minY) minY = wy; if (wy > maxY) maxY = wy;
                }
                fix.aabbMin = Vec2(minX, minY);
                fix.aabbMax = Vec2(maxX, maxY);
                break;
            }
            default:
                fix.aabbMin = Vec2(m_Position.x - 0.5f, m_Position.y - 0.5f);
                fix.aabbMax = Vec2(m_Position.x + 0.5f, m_Position.y + 0.5f);
                break;
        }
    }

    void Bare2DPhysicsBody::RecalculateMass() {
        if (m_Type != BodyType::Dynamic) {
            m_Mass = 0.0f; m_InvMass = 0.0f;
            m_Inertia = 0.0f; m_InvInertia = 0.0f;
            return;
        }

        float32 totalMass = 0.0f;
        float32 totalInertia = 0.0f;

        for (const auto& fix : m_Fixtures) {
            float32 density = fix.material.density;
            float32 area = 0.0f, inertia = 0.0f;

            switch (fix.shape.type) {
                case ShapeType::Circle: {
                    float32 r = fix.shape.circleRadius;
                    area = 3.14159265358979323846f * r * r;
                    inertia = 0.5f * area * r * r;
                    break;
                }
                case ShapeType::Box: {
                    float32 w = 2.0f * fix.shape.boxSize.x;
                    float32 h = 2.0f * fix.shape.boxSize.y;
                    area = w * h;
                    inertia = area * (w * w + h * h) / 12.0f;
                    break;
                }
                case ShapeType::Polygon: {
                    const auto& poly = fix.polygon;
                    if (poly.vertexCount > 0) {
                        float32 minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
                        for (const auto& v : poly.vertices) {
                            if (v.x < minX) minX = v.x; if (v.x > maxX) maxX = v.x;
                            if (v.y < minY) minY = v.y; if (v.y > maxY) maxY = v.y;
                        }
                        float32 w = maxX - minX, h = maxY - minY;
                        area = w * h;
                        inertia = area * (w * w + h * h) / 12.0f;
                    }
                    break;
                }
                default: break;
            }

            totalMass += area * density;
            totalInertia += inertia * density;
        }

        m_Mass = totalMass > 1e-8f ? totalMass : 1.0f;
        m_InvMass = totalMass > 1e-8f ? 1.0f / totalMass : 0.0f;
        m_Inertia = totalInertia > 1e-8f ? totalInertia : 1.0f;
        m_InvInertia = totalInertia > 1e-8f ? 1.0f / totalInertia : 0.0f;
        if (m_FixedRotation) m_InvInertia = 0.0f;
    }

} // namespace Engine