#include "Engine/Core/Physics/Bare2DPhysicsWorld.h"
#include "Engine/Core/Physics/IPhysicsDebugDraw.h"
#include "Engine/Core/JobSystem.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>

namespace Engine {

    // ============================================================
    // 辅助数学函数
    // ============================================================
    namespace {
        float32 Clamp(float32 v, float32 lo, float32 hi) { return std::max(lo, std::min(v, hi)); }

        /// 世界空间点到 Fixture 投影
        bool PointInConvexPolygon(const Vec2& point, const BarePolygon& poly,
                                   const Vec2& pos, float32 angle) {
            float32 c = std::cos(angle), s = std::sin(angle);
            for (int32 i = 0; i < poly.vertexCount; ++i) {
                int32 j = (i + 1) % poly.vertexCount;
                Vec2 vi(poly.vertices[i].x * c - poly.vertices[i].y * s + pos.x,
                        poly.vertices[i].x * s + poly.vertices[i].y * c + pos.y);
                Vec2 vj(poly.vertices[j].x * c - poly.vertices[j].y * s + pos.x,
                        poly.vertices[j].x * s + poly.vertices[j].y * c + pos.y);
                Vec2 edge = vj - vi;
                Vec2 toPoint = point - vi;
                if (Vec2::Cross(edge, toPoint) > 0) return false;
            }
            return true;
        }

        /// 将凸多边形顶点旋转平移到世界空间
        inline void TransformVertices(const BarePolygon& poly, const Vec2& pos, float32 angle,
                                       Vec2* outVerts) {
            float32 c = std::cos(angle), s = std::sin(angle);
            for (int32 i = 0; i < poly.vertexCount; ++i) {
                outVerts[i] = Vec2(
                    poly.vertices[i].x * c - poly.vertices[i].y * s + pos.x,
                    poly.vertices[i].x * s + poly.vertices[i].y * c + pos.y
                );
            }
        }
    }

    // ============================================================
    // 构造 / 析构
    // ============================================================
    Bare2DPhysicsWorld::Bare2DPhysicsWorld(const Vec2& gravity)
        : m_Gravity(gravity)
    {
        m_ForceGenerators.push_back(std::make_shared<GravityForce>());
    }

    Bare2DPhysicsWorld::Bare2DPhysicsWorld(const PhysicsWorldConfig& config)
        : m_Gravity(config.gravity)
        , m_VelocityIterations(config.velocityIterations)
        , m_PositionIterations(config.positionIterations)
        , m_SolverIterations(config.solverIterations)
        , m_UseFixedDt(config.useFixedDt)
        , m_AllowSleep(config.enableSleep)
        , m_EnableCCD(config.enableCCD)
    {
        m_ForceGenerators.push_back(std::make_shared<GravityForce>());
    }

    Bare2DPhysicsWorld::~Bare2DPhysicsWorld() = default;

    // ============================================================
    // Step 管线
    // ============================================================
    void Bare2DPhysicsWorld::Step(float32 dt, int32 velocityIterations, int32 positionIterations) {
        if (dt <= 0.0f) return;

        m_VelocityIterations = velocityIterations;
        m_PositionIterations = positionIterations;

        if (m_UseFixedDt) {
            m_Accumulator += dt;
            while (m_Accumulator >= FIXED_DT) {
                for (auto& body : m_Bodies) {
                    body->SavePrevState();
                }
                FixedStep(FIXED_DT, velocityIterations, positionIterations);
                m_Accumulator -= FIXED_DT;
            }
        } else {
            for (auto& body : m_Bodies) {
                body->SavePrevState();
            }
            FixedStep(dt, velocityIterations, positionIterations);
        }
    }

    void Bare2DPhysicsWorld::FixedStep(float32 dt, int32 velocityIterations, int32 positionIterations) {
        (void)positionIterations;

        // 标准 Semi-Implicit Euler 管线（与 Box2D 一致）:
        //   1. IntegrateForces: v += a * dt (力 → 速度)
        //   2. IntegrateVelocity: x += v * dt (速度 → 位置)
        //   3. DetectCollisions: 在新位置检测碰撞
        //   4. SolveConstraints: 冲量 + 摩擦
        //   5. Baumgarte: 位置推离修正
        //   6. UpdateAABB + WarmStarting + Sleeping + Callbacks

        IntegrateForces(dt);              // v += a*dt
        IntegrateVelocity(dt);            // x += v*dt

        // CCD 在碰撞检测前，检查从原位置到新位置的射线
        if (m_EnableCCD) {
            ComputeCCD(dt);
        }

        std::vector<ContactPair> contacts;
        DetectCollisions(contacts);       // 在新位置检测碰撞

        SolveConstraints(contacts, dt, velocityIterations);  // 修正速度（不修正位置）

        // Baumgarte 位置修正（把穿入地下的物体推出来）
        for (auto& c : contacts) {
            if (c.isSensor) continue;
            Bare2DPhysicsBody* a = c.bodyA;
            Bare2DPhysicsBody* b = c.bodyB;
            float32 totalInvMass = a->GetInvMass() + b->GetInvMass();
            if (totalInvMass < 1e-8f) continue;
            const float32 slop = 0.005f;
            const float32 percent = 0.6f;
            Vec2 correction = c.normal * (std::max(c.penetration - slop, 0.0f) / totalInvMass) * percent;
            a->SetTransform(a->GetPosition() - correction * a->GetInvMass(), a->GetAngle());
            b->SetTransform(b->GetPosition() + correction * b->GetInvMass(), b->GetAngle());
        }

        // 更新 AABB（并行）
        {
            const int32 bodyCount = static_cast<int32>(m_Bodies.size());
            if (m_UseMultithreading && m_JobSystem && bodyCount > 8) {
                m_JobSystem->ParallelFor(0, bodyCount,
                    [this](int32 idx) {
                        UpdateBodyAABB(m_Bodies[idx].get());
                    }
                );
            } else {
                for (auto& body : m_Bodies) {
                    body->UpdateAABB();
                }
            }
        }

        // 更新 Warm Starting 缓存
        {
            std::map<ContactPairKey, ContactManifold> newCache;
            for (auto& c : contacts) {
                ContactPairKey key{c.bodyA, c.bodyB};
                ContactManifold manifold;
                manifold.bodyA = c.bodyA;
                manifold.bodyB = c.bodyB;
                manifold.friction = std::sqrt(c.bodyA->GetFriction() * c.bodyB->GetFriction());
                manifold.restitution = std::max(c.bodyA->GetRestitution(), c.bodyB->GetRestitution());
                manifold.AddPoint(c.contactPoint, c.normal, c.penetration);
                manifold.points[0].normalImpulse = c.normalImpulse;
                manifold.points[0].tangentImpulse = c.tangentImpulse;
                auto prevIt = m_ManifoldCache.find(key);
                if (prevIt != m_ManifoldCache.end()) {
                    manifold.WarmStartFrom(prevIt->second);
                }
                newCache[key] = manifold;
            }
            m_ManifoldCache = std::move(newCache);
        }

        // 分发回调（传感器也触发，用于 Trigger 检测）
        DispatchCallbacks(contacts, dt);

        // 更新活跃碰撞映射
        {
            m_PrevActiveContacts = std::move(m_ActiveContacts);
            ContactMap newMap;
            for (auto& c : contacts) {
                ContactPairKey key{c.bodyA, c.bodyB};
                c.wasColliding = true;
                newMap[key] = c;
            }
            m_ActiveContacts = std::move(newMap);
        }

        // 休眠
        if (m_AllowSleep) {
            UpdateSleeping(dt);
        }
    }

    // ============================================================
    // 创建/销毁刚体
    // ============================================================
    std::shared_ptr<IPhysicsBody> Bare2DPhysicsWorld::CreateBody(const BodyDef& def) {
        auto body = std::make_shared<Bare2DPhysicsBody>(def);
        m_Bodies.push_back(body);
        return body;
    }

    void Bare2DPhysicsWorld::DestroyBody(IPhysicsBody* body) {
        auto* bareBody = static_cast<Bare2DPhysicsBody*>(body);
        auto it = std::find_if(m_Bodies.begin(), m_Bodies.end(),
            [bareBody](const auto& ptr) { return ptr.get() == bareBody; });
        if (it != m_Bodies.end()) {
            m_Bodies.erase(it);
        }

        // 移除相关关节
        m_Joints.erase(
            std::remove_if(m_Joints.begin(), m_Joints.end(),
                [bareBody](const auto& j) {
                    return j->bodyA == bareBody || j->bodyB == bareBody;
                }),
            m_Joints.end()
        );

        // 清理相关接触缓存
        for (auto itC = m_ActiveContacts.begin(); itC != m_ActiveContacts.end(); ) {
            if (itC->first.a == bareBody || itC->first.b == bareBody)
                itC = m_ActiveContacts.erase(itC);
            else ++itC;
        }
        for (auto itC = m_ManifoldCache.begin(); itC != m_ManifoldCache.end(); ) {
            if (itC->first.a == bareBody || itC->first.b == bareBody)
                itC = m_ManifoldCache.erase(itC);
            else ++itC;
        }
    }

    // ============================================================
    // 关节管理
    // ============================================================
    std::shared_ptr<IJoint> Bare2DPhysicsWorld::CreateJoint(const JointDef& def) {
        (void)def;
        // 简化：直接返回 nullptr，需要用户通过引擎框架使用关节
        return nullptr;
    }

    void Bare2DPhysicsWorld::DestroyJoint(IJoint* joint) {
        (void)joint;
    }

    // ============================================================
    // 光线投射
    // ============================================================
    std::vector<RayCastResult> Bare2DPhysicsWorld::RayCast(const Vec2& from, const Vec2& to) {
        std::vector<RayCastResult> results;
        for (auto& body : m_Bodies) {
            if (!body->IsActive()) continue;

            for (int32 fi = 0; fi < body->GetFixtureCount(); ++fi) {
                const auto& fix = body->GetFixture(fi);
                // Ray vs AABB 快速测试
                float32 tHit;
                if (!RayVsAABB(from, to, fix.aabbMin, fix.aabbMax, tHit))
                    continue;

                RayCastResult r;
                r.body = body.get();
                r.point = from + (to - from) * tHit;
                r.fraction = tHit;

                // 近似法线
                Vec2 center = (fix.aabbMin + fix.aabbMax) * 0.5f;
                Vec2 rel = r.point - center;
                float32 overlapX = (fix.aabbMax.x - fix.aabbMin.x) * 0.5f - std::abs(rel.x);
                float32 overlapY = (fix.aabbMax.y - fix.aabbMin.y) * 0.5f - std::abs(rel.y);
                if (overlapX < overlapY)
                    r.normal = Vec2((rel.x > 0) ? 1.0f : -1.0f, 0.0f);
                else
                    r.normal = Vec2(0.0f, (rel.y > 0) ? 1.0f : -1.0f);

                results.push_back(r);
                break; // 每个物体只返回一个结果
            }
        }
        return results;
    }

    std::vector<IPhysicsBody*> Bare2DPhysicsWorld::QueryAABB(const Vec2& center, const Vec2& halfSize) {
        std::vector<IPhysicsBody*> result;
        Vec2 qMin = center - halfSize, qMax = center + halfSize;

        for (auto& body : m_Bodies) {
            if (!body->IsActive()) continue;
            for (int32 fi = 0; fi < body->GetFixtureCount(); ++fi) {
                const auto& fix = body->GetFixture(fi);
                if (fix.aabbMax.x < qMin.x || fix.aabbMin.x > qMax.x) continue;
                if (fix.aabbMax.y < qMin.y || fix.aabbMin.y > qMax.y) continue;
                result.push_back(body.get());
                break;
            }
        }
        return result;
    }

    // ============================================================
    // 力发生器实现
    // ============================================================
    void GravityForce::ApplyForce(IPhysicsBody* body, float32 dt) {
        (void)dt;
        // 重力 = mass * g
        body->ApplyForceToCenter(gravity * body->GetMass());
    }

    void LinearDragForce::ApplyForce(IPhysicsBody* body, float32 dt) {
        (void)dt;
        Vec2 vel = body->GetLinearVelocity();
        // F_drag = -dragCoeff * v² * sign(v)
        Vec2 dragForce(-dragCoeff * vel.x * std::abs(vel.x),
                       -dragCoeff * vel.y * std::abs(vel.y));
        body->ApplyForceToCenter(dragForce);
    }

    // ============================================================
    // 空间网格
    // ============================================================
    int32 Bare2DPhysicsWorld::WorldToGrid(float32 coord) {
        int32 idx = static_cast<int32>(std::floor(coord / GRID_CELL_SIZE)) % GRID_DIM;
        if (idx < 0) idx += GRID_DIM;
        return idx;
    }

    void Bare2DPhysicsWorld::ClearGrid() {
        for (auto& row : m_Grid)
            for (auto& cell : row)
                cell.bodies.clear();
    }

    void Bare2DPhysicsWorld::InsertIntoGrid(Bare2DPhysicsBody* body) {
        Vec2 pos = body->GetPosition();
        int32 gx = WorldToGrid(pos.x);
        int32 gy = WorldToGrid(pos.y);
        m_Grid[gy][gx].bodies.push_back(body);
    }

    void Bare2DPhysicsWorld::GetPotentialPairs(
        std::vector<std::pair<Bare2DPhysicsBody*, Bare2DPhysicsBody*>>& pairs)
    {
        pairs.clear();
        for (auto& row : m_Grid) {
            for (auto& cell : row) {
                auto& bodies = cell.bodies;
                for (size_t i = 0; i < bodies.size(); ++i) {
                    for (size_t j = i + 1; j < bodies.size(); ++j) {
                        if (bodies[i] < bodies[j])
                            pairs.emplace_back(bodies[i], bodies[j]);
                        else
                            pairs.emplace_back(bodies[j], bodies[i]);
                    }
                }
            }
        }
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    }

    // ============================================================
    // 碰撞检测管线
    // ============================================================
    void Bare2DPhysicsWorld::DetectCollisions(std::vector<ContactPair>& contacts) {
        contacts.clear();

        ClearGrid();
        for (auto& body : m_Bodies) {
            if (!body->IsActive()) continue;
            InsertIntoGrid(body.get());
        }

        // 网格内碰撞对检测
        for (int32 gy = 0; gy < GRID_DIM; ++gy) {
            for (int32 gx = 0; gx < GRID_DIM; ++gx) {
                auto& cell = m_Grid[gy][gx];
                if (cell.bodies.size() < 2) continue;

                for (size_t i = 0; i < cell.bodies.size(); ++i) {
                    for (size_t j = i + 1; j < cell.bodies.size(); ++j) {
                        Bare2DPhysicsBody* a = cell.bodies[i];
                        Bare2DPhysicsBody* b = cell.bodies[j];

                        // 碰撞滤波检查
                        if (!ShouldCollide(a->GetCategoryBits(), a->GetMaskBits(),
                                          b->GetCategoryBits(), b->GetMaskBits(),
                                          a->GetGroupIndex(), b->GetGroupIndex()))
                            continue;

                        // 跳过两个都休眠的动态体(静态体始终可碰撞)
                        if (a->GetType() != BodyType::Static && b->GetType() != BodyType::Static) {
                            if (!a->IsAwake() && !b->IsAwake()) continue;
                        }

                        // Fixture 级窄阶段检测
                        for (int32 fi = 0; fi < a->GetFixtureCount(); ++fi) {
                            for (int32 fj = 0; fj < b->GetFixtureCount(); ++fj) {
                                const auto& fixA = a->GetFixture(fi);
                                const auto& fixB = b->GetFixture(fj);

                                // AABB 粗筛
                                if (fixA.aabbMax.x < fixB.aabbMin.x || fixA.aabbMin.x > fixB.aabbMax.x) continue;
                                if (fixA.aabbMax.y < fixB.aabbMin.y || fixA.aabbMin.y > fixB.aabbMax.y) continue;

                                ContactPair pair;
                                bool hit = TestFixtureCollision(fixA, a, fixB, b, pair);
                                if (hit) {
                                    pair.isSensor = fixA.isSensor || fixB.isSensor;
                                    contacts.push_back(pair);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Warm Starting 恢复冲量
        WarmStartContacts(contacts);
    }

    void Bare2DPhysicsWorld::WarmStartContacts(std::vector<ContactPair>& contacts) {
        for (auto& c : contacts) {
            ContactPairKey key{c.bodyA, c.bodyB};
            auto it = m_ManifoldCache.find(key);
            if (it != m_ManifoldCache.end()) {
                const auto& prev = it->second;
                if (prev.pointCount > 0) {
                    c.normalImpulse = prev.points[0].normalImpulse;
                    c.tangentImpulse = prev.points[0].tangentImpulse;
                }
            }
        }
    }

    // ============================================================
    // Fixture 级碰撞检测调度
    // ============================================================
    bool Bare2DPhysicsWorld::TestFixtureCollision(
        const InternalFixture& fixA, Bare2DPhysicsBody* bodyA,
        const InternalFixture& fixB, Bare2DPhysicsBody* bodyB,
        ContactPair& outPair)
    {
        ShapeType ta = fixA.shape.type, tb = fixB.shape.type;

        if (ta == ShapeType::Circle && tb == ShapeType::Circle)
            return TestCircleVsCircle(fixA, bodyA, fixB, bodyB, outPair);
        if (ta == ShapeType::Circle && tb == ShapeType::Box)
            return TestCircleVsAABB(fixA, bodyA, fixB, bodyB, outPair);
        if (ta == ShapeType::Box && tb == ShapeType::Circle)
            return TestCircleVsAABB(fixB, bodyB, fixA, bodyA, outPair);
        if (ta == ShapeType::Circle && tb == ShapeType::Polygon)
            return TestCircleVsPolygon(fixA, bodyA, fixB, bodyB, outPair);
        if (ta == ShapeType::Polygon && tb == ShapeType::Circle)
            return TestCircleVsPolygon(fixB, bodyB, fixA, bodyA, outPair);
        if (ta == ShapeType::Box && tb == ShapeType::Box)
            return TestAABBVsAABB(fixA, bodyA, fixB, bodyB, outPair);
        if (ta == ShapeType::Box && tb == ShapeType::Polygon)
            return TestAABBVsPolygon(fixA, bodyA, fixB, bodyB, outPair);
        if (ta == ShapeType::Polygon && tb == ShapeType::Box)
            return TestAABBVsPolygon(fixB, bodyB, fixA, bodyA, outPair);
        if (ta == ShapeType::Polygon && tb == ShapeType::Polygon)
            return TestPolygonVsPolygon(fixA, bodyA, fixB, bodyB, outPair);

        return false;
    }

    // ============================================================
    // Circle vs Circle
    // ============================================================
    bool Bare2DPhysicsWorld::TestCircleVsCircle(
        const InternalFixture& fixA, Bare2DPhysicsBody* bodyA,
        const InternalFixture& fixB, Bare2DPhysicsBody* bodyB,
        ContactPair& outPair)
    {
        Vec2 diff = bodyB->GetPosition() - bodyA->GetPosition();
        float32 distSq = Vec2::LengthSq(diff);
        float32 rSum = fixA.shape.circleRadius + fixB.shape.circleRadius;
        if (distSq >= rSum * rSum) return false;

        float32 dist = std::sqrt(distSq);
        outPair.bodyA = bodyA;
        outPair.bodyB = bodyB;

        if (dist < 1e-8f) {
            outPair.normal = Vec2(1.0f, 0.0f);
            outPair.contactPoint = bodyA->GetPosition();
            outPair.penetration = rSum;
        } else {
            outPair.normal = diff * (1.0f / dist);
            outPair.contactPoint = bodyA->GetPosition() + outPair.normal * fixA.shape.circleRadius;
            outPair.penetration = rSum - dist;
        }
        return true;
    }

    // ============================================================
    // Circle vs AABB
    // ============================================================
    bool Bare2DPhysicsWorld::TestCircleVsAABB(
        const InternalFixture& fixCircle, Bare2DPhysicsBody* bodyCircle,
        const InternalFixture& fixBox, Bare2DPhysicsBody* bodyBox,
        ContactPair& outPair)
    {
        Vec2 center = bodyCircle->GetPosition();
        Vec2 boxPos = bodyBox->GetPosition();
        Vec2 half = fixBox.shape.boxSize;

        Vec2 closest(
            std::max(boxPos.x - half.x, std::min(center.x, boxPos.x + half.x)),
            std::max(boxPos.y - half.y, std::min(center.y, boxPos.y + half.y))
        );
        Vec2 diff = center - closest;
        float32 distSq = Vec2::LengthSq(diff);
        float32 r = fixCircle.shape.circleRadius;
        if (distSq >= r * r) return false;

        float32 dist = std::sqrt(distSq);
        outPair.bodyA = bodyCircle;
        outPair.bodyB = bodyBox;

        if (dist < 1e-8f) {
            Vec2 toCenter = center - boxPos;
            float32 ox = half.x - std::abs(toCenter.x);
            float32 oy = half.y - std::abs(toCenter.y);
            if (ox < oy) {
                outPair.penetration = ox + r;
                outPair.normal = Vec2((toCenter.x > 0) ? 1.0f : -1.0f, 0.0f);
                outPair.contactPoint = Vec2(boxPos.x + outPair.normal.x * half.x, center.y);
            } else {
                outPair.penetration = oy + r;
                outPair.normal = Vec2(0.0f, (toCenter.y > 0) ? 1.0f : -1.0f);
                outPair.contactPoint = Vec2(center.x, boxPos.y + outPair.normal.y * half.y);
            }
        } else {
            outPair.normal = diff * (1.0f / dist);
            outPair.contactPoint = closest;
            outPair.penetration = r - dist;
        }
        return true;
    }

    // ============================================================
    // Circle vs Polygon
    // ============================================================
    bool Bare2DPhysicsWorld::TestCircleVsPolygon(
        const InternalFixture& fixCircle, Bare2DPhysicsBody* bodyCircle,
        const InternalFixture& fixPoly, Bare2DPhysicsBody* bodyPoly,
        ContactPair& outPair)
    {
        Vec2 center = bodyCircle->GetPosition();
        const auto& polyVerts = fixPoly.polygon.vertices;
        int32 vc = fixPoly.polygon.vertexCount;
        const auto& polyNmls = fixPoly.polygon.normals;
        float32 r = fixCircle.shape.circleRadius;
        Vec2 pos = bodyPoly->GetPosition();
        float32 angle = bodyPoly->GetAngle();

        float32 minDistSq = 1e30f;
        Vec2 nearestEdgeStart, nearestEdgeEnd, nearestNormal;
        float32 c = std::cos(angle), s = std::sin(angle);

        for (int32 i = 0; i < vc; ++i) {
            int32 iNext = (i + 1) % vc;
            Vec2 v0(polyVerts[i].x * c - polyVerts[i].y * s + pos.x,
                    polyVerts[i].x * s + polyVerts[i].y * c + pos.y);
            Vec2 v1(polyVerts[iNext].x * c - polyVerts[iNext].y * s + pos.x,
                    polyVerts[iNext].x * s + polyVerts[iNext].y * c + pos.y);
            Vec2 edge = v1 - v0;
            Vec2 toCircle = center - v0;
            float32 t = Vec2::Dot(toCircle, edge) / Vec2::LengthSq(edge);
            t = Clamp(t, 0.0f, 1.0f);
            Vec2 closestOnEdge = v0 + edge * t;
            Vec2 diff = center - closestOnEdge;
            float32 dSq = Vec2::LengthSq(diff);
            if (dSq < minDistSq) {
                minDistSq = dSq;
                nearestEdgeStart = v0;
                nearestEdgeEnd = v1;
                nearestNormal = Vec2(polyNmls[i].x * c - polyNmls[i].y * s,
                                     polyNmls[i].x * s + polyNmls[i].y * c);
                if (dSq < 1e-8f) break;
            }
        }

        if (minDistSq >= r * r && !PointInConvexPolygon(center, fixPoly.polygon, pos, angle))
            return false;

        float32 dist = std::sqrt(minDistSq);
        bool inside = Vec2::Dot(center - nearestEdgeStart, nearestNormal) < 0;

        outPair.bodyA = bodyCircle;
        outPair.bodyB = bodyPoly;

        if (inside || minDistSq < 1e-8f) {
            outPair.normal = nearestNormal * -1.0f;
            outPair.contactPoint = center;
            outPair.penetration = r + dist;
        } else {
            outPair.normal = (center - nearestEdgeStart) * (1.0f / dist);
            outPair.contactPoint = nearestEdgeStart;
            outPair.penetration = r - dist;
        }
        return true;
    }

    // ============================================================
    // AABB vs AABB
    // ============================================================
    bool Bare2DPhysicsWorld::TestAABBVsAABB(
        const InternalFixture& fixA, Bare2DPhysicsBody* bodyA,
        const InternalFixture& fixB, Bare2DPhysicsBody* bodyB,
        ContactPair& outPair)
    {
        Vec2 aC = bodyA->GetPosition(), bC = bodyB->GetPosition();
        Vec2 aH = fixA.shape.boxSize, bH = fixB.shape.boxSize;
        Vec2 diff = bC - aC;
        float32 ox = (aH.x + bH.x) - std::abs(diff.x);
        float32 oy = (aH.y + bH.y) - std::abs(diff.y);
        if (ox <= 0.0f || oy <= 0.0f) return false;

        outPair.bodyA = bodyA;
        outPair.bodyB = bodyB;
        if (ox < oy) {
            outPair.normal = Vec2((diff.x > 0) ? -1.0f : 1.0f, 0.0f);
            outPair.penetration = ox;
            outPair.contactPoint = Vec2(aC.x + aH.x * ((diff.x > 0) ? 1.0f : -1.0f), aC.y);
        } else {
            outPair.normal = Vec2(0.0f, (diff.y > 0) ? -1.0f : 1.0f);
            outPair.penetration = oy;
            outPair.contactPoint = Vec2(aC.x, aC.y + aH.y * ((diff.y > 0) ? 1.0f : -1.0f));
        }
        return true;
    }

    // ============================================================
    // AABB vs Polygon
    // ============================================================
    bool Bare2DPhysicsWorld::TestAABBVsPolygon(
        const InternalFixture& fixBox, Bare2DPhysicsBody* bodyBox,
        const InternalFixture& fixPoly, Bare2DPhysicsBody* bodyPoly,
        ContactPair& outPair)
    {
        // 将 Polygon 近似为 AABB 做检测（简化）
        Vec2 polyMin(1e30f, 1e30f), polyMax(-1e30f, -1e30f);
        const auto& verts = fixPoly.polygon.vertices;
        Vec2 pos = bodyPoly->GetPosition();
        float32 angle = bodyPoly->GetAngle();
        float32 c = std::cos(angle), s = std::sin(angle);

        for (const auto& v : verts) {
            float32 wx = v.x * c - v.y * s + pos.x;
            float32 wy = v.x * s + v.y * c + pos.y;
            if (wx < polyMin.x) polyMin.x = wx;
            if (wy < polyMin.y) polyMin.y = wy;
            if (wx > polyMax.x) polyMax.x = wx;
            if (wy > polyMax.y) polyMax.y = wy;
        }

        Vec2 polyCenter = (polyMin + polyMax) * 0.5f;
        Vec2 polyHalf = (polyMax - polyMin) * 0.5f;

        Vec2 aC = bodyBox->GetPosition();
        Vec2 aH = fixBox.shape.boxSize;
        Vec2 diff = polyCenter - aC;
        float32 ox = (aH.x + polyHalf.x) - std::abs(diff.x);
        float32 oy = (aH.y + polyHalf.y) - std::abs(diff.y);
        if (ox <= 0.0f || oy <= 0.0f) return false;

        outPair.bodyA = bodyBox;
        outPair.bodyB = bodyPoly;
        if (ox < oy) {
            outPair.normal = Vec2((diff.x > 0) ? -1.0f : 1.0f, 0.0f);
            outPair.penetration = ox;
            outPair.contactPoint = Vec2(aC.x + aH.x * ((diff.x > 0) ? 1.0f : -1.0f), aC.y);
        } else {
            outPair.normal = Vec2(0.0f, (diff.y > 0) ? -1.0f : 1.0f);
            outPair.penetration = oy;
            outPair.contactPoint = Vec2(aC.x, aC.y + aH.y * ((diff.y > 0) ? 1.0f : -1.0f));
        }
        return true;
    }

    // ============================================================
    // Polygon vs Polygon (SAT)
    // ============================================================
    bool Bare2DPhysicsWorld::TestPolygonVsPolygon(
        const InternalFixture& fixA, Bare2DPhysicsBody* bodyA,
        const InternalFixture& fixB, Bare2DPhysicsBody* bodyB,
        ContactPair& outPair)
    {
        return SATOverlap(fixA.polygon, bodyA, fixB.polygon, bodyB, outPair);
    }

    float32 Bare2DPhysicsWorld::ProjectPolygonOnAxis(
        const BarePolygon& poly, const Vec2& axis,
        const Vec2& pos, float32 angle,
        float32& outMin, float32& outMax)
    {
        poly.ProjectOntoAxis(axis, pos, angle, outMin, outMax);
        return outMax - outMin;
    }

    bool Bare2DPhysicsWorld::SATOverlap(
        const BarePolygon& polyA, Bare2DPhysicsBody* bodyA,
        const BarePolygon& polyB, Bare2DPhysicsBody* bodyB,
        ContactPair& outPair)
    {
        Vec2 posA = bodyA->GetPosition(), posB = bodyB->GetPosition();
        float32 angA = bodyA->GetAngle(), angB = bodyB->GetAngle();
        int32 vcA = polyA.vertexCount, vcB = polyB.vertexCount;

        float32 minOverlap = 1e30f;
        Vec2 minAxis(0, 0);
        bool found = true;

        // 收集所有待测试轴（两个多边形的所有法线）
        const int32 MAX_AXES = 32;
        Vec2 axes[MAX_AXES];
        int32 axisCount = 0;

        // 旋转法线到世界空间
        float32 cA = std::cos(angA), sA = std::sin(angA);
        float32 cB = std::cos(angB), sB = std::sin(angB);

        for (int32 i = 0; i < vcA && axisCount < MAX_AXES; ++i) {
            axes[axisCount++] = Vec2(polyA.normals[i].x * cA - polyA.normals[i].y * sA,
                                      polyA.normals[i].x * sA + polyA.normals[i].y * cA);
        }
        for (int32 i = 0; i < vcB && axisCount < MAX_AXES; ++i) {
            axes[axisCount++] = Vec2(polyB.normals[i].x * cB - polyB.normals[i].y * sB,
                                      polyB.normals[i].x * sB + polyB.normals[i].y * cB);
        }

        for (int32 i = 0; i < axisCount; ++i) {
            Vec2 axis = axes[i];
            if (Vec2::LengthSq(axis) < 1e-8f) continue;
            axis = Vec2::Normalize(axis);

            float32 minA, maxA, minB, maxB;
            polyA.ProjectOntoAxis(axis, posA, angA, minA, maxA);
            polyB.ProjectOntoAxis(axis, posB, angB, minB, maxB);

            // 检查分离
            if (maxA < minB || maxB < minA) { found = false; break; }

            float32 overlap = std::min(maxA - minB, maxB - minA);
            if (overlap < minOverlap) {
                minOverlap = overlap;
                minAxis = axis;
            }
        }

        if (!found) return false;

        // 确保法线指向 A→B
        Vec2 centerDiff = posB - posA;
        if (Vec2::Dot(centerDiff, minAxis) < 0)
            minAxis = minAxis * -1.0f;

        outPair.bodyA = bodyA;
        outPair.bodyB = bodyB;
        outPair.normal = minAxis;
        outPair.penetration = minOverlap;
        outPair.contactPoint = (posA + posB) * 0.5f;
        return true;
    }

    // ============================================================
    // 约束求解器（Sequential Impulses）
    // ============================================================
    void Bare2DPhysicsWorld::SolveConstraints(std::vector<ContactPair>& contacts, float32 dt, int32 iterations) {
        // 多次迭代提高收敛性
        for (int32 iter = 0; iter < iterations; ++iter) {
            for (auto& c : contacts) {
                if (c.isSensor) continue;

                Bare2DPhysicsBody* a = c.bodyA;
                Bare2DPhysicsBody* b = c.bodyB;

                // PreSolve 回调
                if (m_ContactPreSolveCallback) {
                    if (!m_ContactPreSolveCallback(a, b))
                        continue;
                }

                ResolveContact(a, b, c.contactPoint, c.normal, c.penetration,
                              c.normalImpulse, c.tangentImpulse, dt);
            }

            // 求解关节约束
            for (auto& joint : m_Joints) {
                joint->Solve(dt);
            }
        }
    }

    void Bare2DPhysicsWorld::ResolveContact(
        Bare2DPhysicsBody* a, Bare2DPhysicsBody* b,
        const Vec2& contactPoint, const Vec2& normal,
        float32 penetration, float32& normalImpulse,
        float32& tangentImpulse, float32 dt)
    {
        // 注意：位置修正（Baumgarte）已在 FixedStep 的外层循环完成
        // 此处只做速度/冲量修正
        constexpr float32 slop = 0.01f;
        float32 totalInvMass = a->GetInvMass() + b->GetInvMass();
        if (totalInvMass < 1e-8f) return;

        // 相对速度计算（含角速度影响）
        Vec2 rA = contactPoint - a->GetPosition();
        Vec2 rB = contactPoint - b->GetPosition();
        Vec2 velA = a->GetLinearVelocity() + Vec2(-a->GetAngularVelocity() * rA.y, a->GetAngularVelocity() * rA.x);
        Vec2 velB = b->GetLinearVelocity() + Vec2(-b->GetAngularVelocity() * rB.y, b->GetAngularVelocity() * rB.x);
        Vec2 relVel = velB - velA;

        // 法线方向冲量
        float32 relVelNormal = Vec2::Dot(relVel, normal);
        if (relVelNormal > 0.0f && penetration < slop) return; // 正在分离

        float32 e = std::max(a->GetRestitution(), b->GetRestitution());
        float32 raCrossN = Vec2::Cross(rA, normal);
        float32 rbCrossN = Vec2::Cross(rB, normal);
        float32 invMassSum = a->GetInvMass() + b->GetInvMass()
            + a->GetInvInertia() * raCrossN * raCrossN
            + b->GetInvInertia() * rbCrossN * rbCrossN;

        if (invMassSum < 1e-8f) return;

        // Accumulated impulse (Warm Starting compatible)
        float32 j = -(1.0f + e) * relVelNormal / invMassSum;
        float32 newNormalImpulse = std::max(normalImpulse + j, 0.0f);
        j = newNormalImpulse - normalImpulse;
        normalImpulse = newNormalImpulse;

        Vec2 impulse = normal * j;
        a->SetLinearVelocity(a->GetLinearVelocity() - impulse * a->GetInvMass());
        a->SetAngularVelocity(a->GetAngularVelocity() - a->GetInvInertia() * Vec2::Cross(rA, impulse));
        b->SetLinearVelocity(b->GetLinearVelocity() + impulse * b->GetInvMass());
        b->SetAngularVelocity(b->GetAngularVelocity() + b->GetInvInertia() * Vec2::Cross(rB, impulse));

        // 摩擦冲量（Coulomb 摩擦模型）
        relVel = (b->GetLinearVelocity() + Vec2(-b->GetAngularVelocity() * rB.y, b->GetAngularVelocity() * rB.x))
               - (a->GetLinearVelocity() + Vec2(-a->GetAngularVelocity() * rA.y, a->GetAngularVelocity() * rA.x));
        Vec2 tangent = relVel - normal * Vec2::Dot(relVel, normal);
        float32 tangLenSq = Vec2::LengthSq(tangent);
        if (tangLenSq > 1e-8f) {
            tangent = tangent * (1.0f / std::sqrt(tangLenSq));
            float32 relVelTang = Vec2::Dot(relVel, tangent);
            float32 raCrossT = Vec2::Cross(rA, tangent);
            float32 rbCrossT = Vec2::Cross(rB, tangent);
            float32 invMassSumT = a->GetInvMass() + b->GetInvMass()
                + a->GetInvInertia() * raCrossT * raCrossT
                + b->GetInvInertia() * rbCrossT * rbCrossT;

            if (invMassSumT > 1e-8f) {
                float32 friction = std::sqrt(a->GetFriction() * b->GetFriction());
                float32 jt = -relVelTang / invMassSumT;

                // Coulomb 限制 |ft| <= μ * fn
                float32 maxFriction = normalImpulse * friction;
                float32 newTangentImpulse = Clamp(tangentImpulse + jt, -maxFriction, maxFriction);
                jt = newTangentImpulse - tangentImpulse;
                tangentImpulse = newTangentImpulse;

                Vec2 frictionImpulse = tangent * jt;
                a->SetLinearVelocity(a->GetLinearVelocity() - frictionImpulse * a->GetInvMass());
                a->SetAngularVelocity(a->GetAngularVelocity() - a->GetInvInertia() * Vec2::Cross(rA, frictionImpulse));
                b->SetLinearVelocity(b->GetLinearVelocity() + frictionImpulse * b->GetInvMass());
                b->SetAngularVelocity(b->GetAngularVelocity() + b->GetInvInertia() * Vec2::Cross(rB, frictionImpulse));
            }
        }
    }

    // ============================================================
    // 回调分发
    // ============================================================
    void Bare2DPhysicsWorld::DispatchCallbacks(
        const std::vector<ContactPair>& newContacts, float32 dt)
    {
        (void)dt;
        ContactMap newMap;
        for (auto& c : newContacts) {
            ContactPairKey key{c.bodyA, c.bodyB};
            auto it = m_ActiveContacts.find(key);
            if (it == m_ActiveContacts.end()) {
                // Begin
                if (m_ContactBeginCallback && !c.isSensor) {
                    ContactInfo info;
                    info.bodyA = c.bodyA;
                    info.bodyB = c.bodyB;
                    info.point = c.contactPoint;
                    info.normal = c.normal;
                    info.impulse = c.normalImpulse;
                    m_ContactBeginCallback(info);
                }
            } else {
                // Persist
                if (m_ContactPersistCallback && !c.isSensor) {
                    ContactPersistData data;
                    data.bodyA = c.bodyA;
                    data.bodyB = c.bodyB;
                    data.point = c.contactPoint;
                    data.normal = c.normal;
                    data.impulse = c.normalImpulse;
                    data.pointCount = 1;
                    data.impulses[0] = c.normalImpulse;
                    m_ContactPersistCallback(data);
                }
            }
            newMap[key] = c;
        }

        // End
        for (auto& [key, oldContact] : m_PrevActiveContacts) {
            if (newMap.find(key) == newMap.end()) {
                if (m_ContactEndCallback && !oldContact.isSensor) {
                    ContactInfo info;
                    info.bodyA = oldContact.bodyA;
                    info.bodyB = oldContact.bodyB;
                    info.point = oldContact.contactPoint;
                    info.normal = oldContact.normal;
                    info.impulse = oldContact.normalImpulse;
                    m_ContactEndCallback(info);
                }
            }
        }
    }

    // ============================================================
    // CCD（连续碰撞检测）
    // ============================================================
    bool Bare2DPhysicsWorld::RayVsAABB(const Vec2& from, const Vec2& to,
                                        const Vec2& aabbMin, const Vec2& aabbMax,
                                        float32& outT) {
        Vec2 dir = to - from;
        float32 tMin = 0.0f, tMax = 1.0f;

        for (int32 axis = 0; axis < 2; ++axis) {
            float32 invD = 1.0f / ((axis == 0) ? dir.x : dir.y);
            float32 t1 = ((axis == 0 ? aabbMin.x : aabbMin.y) - (axis == 0 ? from.x : from.y)) * invD;
            float32 t2 = ((axis == 0 ? aabbMax.x : aabbMax.y) - (axis == 0 ? from.x : from.y)) * invD;
            if (t1 > t2) std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) return false;
        }
        outT = tMin;
        return tMin >= 0.0f && tMin <= 1.0f;
    }

    void Bare2DPhysicsWorld::ComputeCCD(float32 dt) {
        for (auto& body : m_Bodies) {
            if (!body->IsActive() || body->GetType() != BodyType::Dynamic)
                continue;
            if (!body->IsBullet()) continue; // 只处理标记为 bullet 的物体

            Vec2 vel = body->GetLinearVelocity();
            float32 speed = Vec2::Length(vel);
            if (speed * dt < body->GetBoxHalfSize().x * 0.5f) continue; // 速度不够快

            Vec2 start = body->GetPosition();
            Vec2 end = start + vel * dt;

            for (auto& other : m_Bodies) {
                if (other == body || !other->IsActive()) continue;
                if (other->GetType() != BodyType::Static && other->GetType() != BodyType::Kinematic)
                    continue;

                for (int32 fi = 0; fi < other->GetFixtureCount(); ++fi) {
                    const auto& fix = other->GetFixture(fi);
                    float32 tHit;
                    if (!RayVsAABB(start, end, fix.aabbMin, fix.aabbMax, tHit))
                        continue;

                    if (tHit >= 0.0f && tHit <= 1.0f) {
                        Vec2 newPos = start + vel * (tHit * dt);
                        body->SetTransform(newPos, body->GetAngle());
                        body->SetLinearVelocity(Vec2(0, 0));
                        return; // 只处理第一个碰撞
                    }
                }
            }
        }
    }

    // ============================================================
    // 积分器
    // ============================================================
    void Bare2DPhysicsWorld::IntegrateForces(float32 dt) {
        // 应用所有力发生器（必须在主线程串行执行，因为力发生器可能共享状态）
        for (auto& gen : m_ForceGenerators) {
            if (!gen->IsValid()) continue;
            for (auto& body : m_Bodies) {
                if (body->GetType() != BodyType::Dynamic || !body->IsActive() || !body->IsAwake())
                    continue;
                gen->ApplyForce(body.get(), dt);
            }
        }

        // 并行积分：力 → 速度（每体独立，无数据竞争）
        const int32 bodyCount = static_cast<int32>(m_Bodies.size());
        if (m_UseMultithreading && m_JobSystem && bodyCount > 16) {
            m_JobSystem->ParallelFor(0, bodyCount,
                [this, dt](int32 idx) {
                    IntegrateBodyForces(m_Bodies[idx].get(), dt);
                }
            );
        } else {
            for (auto& body : m_Bodies) {
                IntegrateBodyForces(body.get(), dt);
            }
        }
    }

    void Bare2DPhysicsWorld::IntegrateBodyForces(Bare2DPhysicsBody* body, float32 dt) {
        if (body->GetType() != BodyType::Dynamic || !body->IsActive() || !body->IsAwake())
            return;

        // 线性
        Vec2 force = body->GetForceAccum();
        Vec2 acceleration = force * body->GetInvMass();
        Vec2 newVel = body->GetLinearVelocity() + acceleration * dt;

        // 阻尼
        newVel = newVel * std::max(0.0f, 1.0f - body->GetLinearDamping() * dt);
        body->SetLinearVelocity(newVel);

        // 角度
        float32 angAccel = body->GetInvInertia() * body->GetTorqueAccum();
        float32 newAngVel = body->GetAngularVelocity() + angAccel * dt;
        newAngVel *= std::max(0.0f, 1.0f - body->GetAngularDamping() * dt);
        body->SetAngularVelocity(newAngVel);

        // 清空力累加器
        body->ClearForceAccum();
    }

    void Bare2DPhysicsWorld::IntegrateVelocity(float32 dt) {
        const int32 bodyCount = static_cast<int32>(m_Bodies.size());
        if (m_UseMultithreading && m_JobSystem && bodyCount > 16) {
            m_JobSystem->ParallelFor(0, bodyCount,
                [this, dt](int32 idx) {
                    IntegrateBodyVelocity(m_Bodies[idx].get(), dt);
                }
            );
        } else {
            for (auto& body : m_Bodies) {
                IntegrateBodyVelocity(body.get(), dt);
            }
        }
    }

    void Bare2DPhysicsWorld::IntegrateBodyVelocity(Bare2DPhysicsBody* body, float32 dt) {
        if (body->GetType() != BodyType::Dynamic || !body->IsActive() || !body->IsAwake())
            return;

        Vec2 vel = body->GetLinearVelocity();
        body->SetTransform(
            body->GetPosition() + vel * dt,
            body->GetAngle() + body->GetAngularVelocity() * dt
        );
    }

    void Bare2DPhysicsWorld::UpdateBodyAABB(Bare2DPhysicsBody* body) {
        body->UpdateAABB();
    }

    // ============================================================
    // 休眠系统
    // ============================================================
    void Bare2DPhysicsWorld::UpdateSleeping(float32 dt) {
        // 并行更新每体的休眠计时器
        const int32 bodyCount = static_cast<int32>(m_Bodies.size());
        if (m_UseMultithreading && m_JobSystem && bodyCount > 16) {
            m_JobSystem->ParallelFor(0, bodyCount,
                [this, dt](int32 idx) {
                    UpdateBodySleeping(m_Bodies[idx].get(), dt);
                }
            );
        } else {
            for (auto& body : m_Bodies) {
                UpdateBodySleeping(body.get(), dt);
            }
        }

        // 如果碰撞对中有唤醒的物体，唤醒沉睡的邻居（必须串行）
        for (auto& kv : m_ActiveContacts) {
            auto* a = kv.second.bodyA;
            auto* b = kv.second.bodyB;
            if (a->IsAwake() && !b->IsAwake()) {
                b->SetAwake(true);
            }
            if (b->IsAwake() && !a->IsAwake()) {
                a->SetAwake(true);
            }
        }
    }

    void Bare2DPhysicsWorld::UpdateBodySleeping(Bare2DPhysicsBody* body, float32 dt) {
        if (body->GetType() != BodyType::Dynamic || !body->IsActive())
            return;
        if (!body->IsAwake()) return;

        float32 speed = Vec2::Length(body->GetLinearVelocity())
                      + std::abs(body->GetAngularVelocity());

        if (speed < Bare2DPhysicsBody::SLEEP_SPEED_THRESHOLD) {
            body->SetSleepTimer(body->GetSleepTimer() + dt);
            if (body->GetSleepTimer() > Bare2DPhysicsBody::SLEEP_TIME_THRESHOLD) {
                body->SetAwake(false);
            }
        } else {
            body->SetSleepTimer(0.0f);
        }
    }

    // ============================================================
    // 形状投射 (Shapecast)
    // ============================================================
    std::vector<ShapeCastResult> Bare2DPhysicsWorld::ShapeCast(
        const Vec2& from, const Vec2& to,
        ShapeType type, const Vec2& size)
    {
        std::vector<ShapeCastResult> results;
        Vec2 dir = to - from;
        float32 maxDist = Vec2::Length(dir);
        if (maxDist < 1e-8f) return results;
        dir = dir * (1.0f / maxDist);

        Vec2 halfSize = size * 0.5f;

        for (auto& body : m_Bodies) {
            if (!body->IsActive()) continue;

            // 对物体的每个 Fixture 测试
            for (int32 fi = 0; fi < body->GetFixtureCount(); ++fi) {
                const auto& fix = body->GetFixture(fi);

                // 简化：用 AABB 替代
                float32 tHit;
                Vec2 expandedMin = fix.aabbMin - halfSize;
                Vec2 expandedMax = fix.aabbMax + halfSize;
                if (!RayVsAABB(from, to, expandedMin, expandedMax, tHit))
                    continue;

                ShapeCastResult result;
                result.body = body.get();
                result.point = from + dir * (tHit * maxDist);
                result.fraction = tHit;
                result.normal = Vec2(0, 0);
                results.push_back(result);
                break;
            }
        }
        return results;
    }

    // ============================================================
    // Trigger 查询
    // ============================================================
    std::vector<IPhysicsBody*> Bare2DPhysicsWorld::GetTriggerOverlaps(IPhysicsBody* body) {
        std::vector<IPhysicsBody*> results;
        auto* bareBody = static_cast<Bare2DPhysicsBody*>(body);
        if (!bareBody || !bareBody->IsActive()) return results;

        for (auto& other : m_Bodies) {
            if (other.get() == bareBody || !other->IsActive()) continue;

            // 快速 AABB 检测
            bool aabbOverlap = false;
            for (int32 fi = 0; fi < bareBody->GetFixtureCount() && !aabbOverlap; ++fi) {
                const auto& fixA = bareBody->GetFixture(fi);
                for (int32 fj = 0; fj < other->GetFixtureCount(); ++fj) {
                    const auto& fixB = other->GetFixture(fj);
                    if (fixA.aabbMax.x < fixB.aabbMin.x || fixA.aabbMin.x > fixB.aabbMax.x) continue;
                    if (fixA.aabbMax.y < fixB.aabbMin.y || fixA.aabbMin.y > fixB.aabbMax.y) continue;
                    aabbOverlap = true;
                    break;
                }
            }

            if (aabbOverlap) {
                results.push_back(other.get());
            }
        }
        return results;
    }

    // ============================================================
    // 调试绘制
    // ============================================================
    void Bare2DPhysicsWorld::DebugDraw() {
        if (!m_DebugDraw) return;

        for (auto& body : m_Bodies) {
            if (!body->IsActive()) continue;

            Vec2 pos = body->GetPosition();
            DebugColor color = body->IsAwake() ?
                DebugColor(0.0f, 1.0f, 0.0f, 1.0f) :   // 绿色 = 激活
                DebugColor(0.5f, 0.5f, 0.5f, 1.0f);   // 灰色 = 休眠

            for (int32 fi = 0; fi < body->GetFixtureCount(); ++fi) {
                const auto& fix = body->GetFixture(fi);
                float32 angle = body->GetAngle();

                switch (fix.shape.type) {
                    case ShapeType::Circle:
                        m_DebugDraw->DrawCircle(pos, fix.shape.circleRadius, color);
                        break;

                    case ShapeType::Box: {
                        Vec2 h = fix.shape.boxSize;
                        float32 c = std::cos(angle), s = std::sin(angle);
                        Vec2 verts[4] = {
                            Vec2(-h.x * c + h.y * s + pos.x, -h.x * s - h.y * c + pos.y),
                            Vec2( h.x * c + h.y * s + pos.x,  h.x * s - h.y * c + pos.y),
                            Vec2( h.x * c - h.y * s + pos.x,  h.x * s + h.y * c + pos.y),
                            Vec2(-h.x * c - h.y * s + pos.x, -h.x * s + h.y * c + pos.y)
                        };
                        m_DebugDraw->DrawPolygon(verts, 4, color);
                        break;
                    }

                    case ShapeType::Polygon: {
                        const auto& poly = fix.polygon;
                        if (poly.vertexCount < 3) break;
                        Vec2 verts[16];
                        float32 c = std::cos(angle), s = std::sin(angle);
                        for (int32 i = 0; i < poly.vertexCount; ++i) {
                            verts[i] = Vec2(
                                poly.vertices[i].x * c - poly.vertices[i].y * s + pos.x,
                                poly.vertices[i].x * s + poly.vertices[i].y * c + pos.y
                            );
                        }
                        m_DebugDraw->DrawPolygon(verts, poly.vertexCount, color);
                        break;
                    }

                    default: break;
                }

                // 绘制 AABB 边框（调试用）
                DebugColor aabbColor(1.0f, 1.0f, 0.0f, 0.5f);
                Vec2 aabbVerts[4] = {
                    Vec2(fix.aabbMin.x, fix.aabbMin.y),
                    Vec2(fix.aabbMax.x, fix.aabbMin.y),
                    Vec2(fix.aabbMax.x, fix.aabbMax.y),
                    Vec2(fix.aabbMin.x, fix.aabbMax.y)
                };
                m_DebugDraw->DrawPolygon(aabbVerts, 4, aabbColor);
            }

            // 绘制碰撞法线
            for (auto& kv : m_ActiveContacts) {
                if (kv.second.bodyA == body.get() || kv.second.bodyB == body.get()) {
                    DebugColor contactColor(1.0f, 0.0f, 0.0f, 1.0f);
                    m_DebugDraw->DrawCircle(kv.second.contactPoint, 0.05f, contactColor);
                    m_DebugDraw->DrawSegment(
                        kv.second.contactPoint,
                        kv.second.contactPoint + kv.second.normal * 0.3f,
                        contactColor
                    );
                }
            }
        }
    }

    // ============================================================
    // 同步方法
    // ============================================================
    void Bare2DPhysicsWorld::SyncTransformToBody(
        IPhysicsBody* body, const Vec2& position, float32 angle)
    {
        if (body) body->SetTransform(position, angle);
    }

    void Bare2DPhysicsWorld::SyncBodyToTransform(
        const IPhysicsBody* body, Vec2& outPosition, float32& outAngle)
    {
        if (body) {
            outPosition = body->GetPosition();
            outAngle = body->GetAngle();
        } else {
            outPosition = Vec2(0, 0);
            outAngle = 0;
        }
    }

    Vec2 Bare2DPhysicsWorld::InterpolatePosition(const IPhysicsBody* body, float32 alpha) {
        if (!body) return Vec2(0, 0);
        auto* bareBody = static_cast<const Bare2DPhysicsBody*>(body);
        return bareBody->GetInterpPosition(alpha);
    }

    float32 Bare2DPhysicsWorld::InterpolateAngle(const IPhysicsBody* body, float32 alpha) {
        if (!body) return 0;
        auto* bareBody = static_cast<const Bare2DPhysicsBody*>(body);
        return bareBody->GetInterpAngle(alpha);
    }

} // namespace Engine