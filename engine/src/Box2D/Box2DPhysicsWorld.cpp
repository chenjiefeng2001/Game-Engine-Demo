#include "Box2DPhysicsWorld.h"   // => Engine/Box2D/Box2DPhysicsWorld.h + box2d/id.h
#include "Box2DPhysicsBody.h"
#include "Box2DJoint.h"
#include "Box2DDebugDraw.h"
#include "Box2DContactListener.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include <box2d/box2d.h>
#include <algorithm>
#include <cmath>

namespace Engine {

    // ============================================================
    // 匿名命名空间：引擎类型 ↔ Box2D 类型转换
    // ============================================================
    namespace {

        b2Vec2 ToB2(const Vec2& v) {
            return b2Vec2{v.x, v.y};
        }

        Vec2 FromB2(b2Vec2 v) {
            return Vec2(v.x, v.y);
        }

        b2BodyType ToB2Type(BodyType type) {
            switch (type) {
                case BodyType::Static:    return b2_staticBody;
                case BodyType::Dynamic:   return b2_dynamicBody;
                case BodyType::Kinematic: return b2_kinematicBody;
                default:                  break;
            }
            return b2_staticBody;
        }

        BodyType FromB2Type(b2BodyType type) {
            switch (type) {
                case b2_staticBody:    return BodyType::Static;
                case b2_dynamicBody:   return BodyType::Dynamic;
                case b2_kinematicBody: return BodyType::Kinematic;
                default:               break;
            }
            return BodyType::Static;
        }

        /// 从 b2BodyId 获取 IPhysicsBody*
        IPhysicsBody* GetBodyUserData(b2BodyId bodyId) {
            if (!b2Body_IsValid(bodyId)) return nullptr;
            return static_cast<IPhysicsBody*>(b2Body_GetUserData(bodyId));
        }

        /// 内部工具：将碰撞事件路由到 PhysicsComponent 级别
        void RouteContactEvent(const ContactInfo& info,
                                void (*invoke)(const PhysicsComponent*, const ContactInfo&)) {
            if (!info.bodyA || !info.bodyB) return;
            auto* bodyA = static_cast<const IPhysicsBody*>(info.bodyA);
            auto* bodyB = static_cast<const IPhysicsBody*>(info.bodyB);
            auto* compA = static_cast<PhysicsComponent*>(bodyA->GetComponentRef());
            auto* compB = static_cast<PhysicsComponent*>(bodyB->GetComponentRef());
            if (compA) invoke(compA, info);
            if (compB) invoke(compB, info);
        }

        void RoutePersistEvent(const ContactPersistData& data) {
            if (!data.bodyA || !data.bodyB) return;
            auto* bodyA = static_cast<const IPhysicsBody*>(data.bodyA);
            auto* bodyB = static_cast<const IPhysicsBody*>(data.bodyB);
            auto* compA = static_cast<PhysicsComponent*>(bodyA->GetComponentRef());
            auto* compB = static_cast<PhysicsComponent*>(bodyB->GetComponentRef());
            if (compA) compA->InvokeCollisionStay(data);
            if (compB) compB->InvokeCollisionStay(data);
        }

        /// 在刚体上创建初始形状（用于 CreateBody）
        void CreateShapesFromBodyDef(b2BodyId bodyId, const BodyDef& def) {
            b2ShapeDef shapeDef = b2DefaultShapeDef();
            shapeDef.density     = def.density;
            shapeDef.material.friction    = def.friction;
            shapeDef.material.restitution = def.restitution;
            shapeDef.filter.categoryBits = static_cast<uint64_t>(def.categoryBits);
            shapeDef.filter.maskBits     = static_cast<uint64_t>(def.maskBits);
            shapeDef.filter.groupIndex   = def.groupIndex;
            shapeDef.enableContactEvents = true;

            switch (def.shape.type) {
                case ShapeType::Box: {
                    b2Polygon poly = b2MakeOffsetBox(
                        def.shape.boxSize.x, def.shape.boxSize.y,
                        ToB2(def.shape.offset), b2MakeRot(0.0f));
                    b2CreatePolygonShape(bodyId, &shapeDef, &poly);
                    break;
                }
                case ShapeType::Circle: {
                    b2Circle circle;
                    circle.center = ToB2(def.shape.offset);
                    circle.radius = def.shape.circleRadius;
                    b2CreateCircleShape(bodyId, &shapeDef, &circle);
                    break;
                }
                case ShapeType::Edge: {
                    b2Segment seg;
                    seg.point1 = ToB2(def.shape.edgeStart);
                    seg.point2 = ToB2(def.shape.edgeEnd);
                    b2CreateSegmentShape(bodyId, &shapeDef, &seg);
                    break;
                }
                case ShapeType::Chain:
                    // 链形状在 Box2DPhysicsBody::AddFixture 中处理
                    break;
            }
        }

    } // anonymous namespace

    // ============================================================
    // 构造 / 析构
    // ============================================================

    Box2DPhysicsWorld::Box2DPhysicsWorld(const Vec2& gravity) {
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = ToB2(gravity);
        // 使用单线程（设置 workerCount=1 避免线程开销）
        worldDef.workerCount = 1;
        m_WorldId = b2CreateWorld(&worldDef);
    }

    Box2DPhysicsWorld::~Box2DPhysicsWorld() {
        // shared_ptr 会自动释放，先清理容器
        m_Joints.clear();
        m_Bodies.clear();

        if (b2World_IsValid(m_WorldId)) {
            b2DestroyWorld(m_WorldId);
        }
        m_WorldId = {};
    }

    // ============================================================
    // 步进 + 碰撞事件轮询
    // ============================================================

    void Box2DPhysicsWorld::Step(float32 dt,
                                 int32 velocityIterations,
                                 int32 positionIterations) {
        if (!b2World_IsValid(m_WorldId)) return;

        // v3 使用 subStepCount 代替 velocity/position iterations
        // 将迭代次数映射到子步数：取平均值 / 2
        int32 subStepCount = std::max(1, (velocityIterations + positionIterations) / 4);
        b2World_Step(m_WorldId, dt, subStepCount);

        // 轮询碰撞事件
        PollContactEvents();
    }

    // ============================================================
    // PollContactEvents — 处理 Step 后的碰撞事件
    // ============================================================

    void Box2DPhysicsWorld::PollContactEvents() {
        b2ContactEvents events = b2World_GetContactEvents(m_WorldId);

        // Begin Contact
        for (int i = 0; i < events.beginCount; ++i) {
            ContactInfo info = Box2DContactListener::MakeContactInfo(events.beginEvents[i]);
            OnContactBegin(info);
        }

        // End Contact
        for (int i = 0; i < events.endCount; ++i) {
            ContactInfo info = Box2DContactListener::MakeContactInfoEnd(events.endEvents[i]);
            OnContactEnd(info);
        }

        // Hit events (持续接触 + 冲量)
        for (int i = 0; i < events.hitCount; ++i) {
            ContactPersistData data = Box2DContactListener::MakeContactPersistData(events.hitEvents[i]);
            OnContactPersist(data);
        }
    }

    // ============================================================
    // 刚体管理
    // ============================================================

    std::shared_ptr<IPhysicsBody> Box2DPhysicsWorld::CreateBody(const BodyDef& def) {
        if (!b2World_IsValid(m_WorldId)) return nullptr;

        b2BodyDef bd = b2DefaultBodyDef();
        bd.type          = ToB2Type(def.type);
        bd.position      = ToB2(def.position);
        bd.linearDamping = def.linearDamping;
        bd.angularDamping= def.angularDamping;
        bd.isAwake       = def.allowSleep;

        b2BodyId bodyId = b2CreateBody(m_WorldId, &bd);
        if (!b2Body_IsValid(bodyId)) return nullptr;

        // 创建初始形状
        CreateShapesFromBodyDef(bodyId, def);

        // 创建包装对象
        auto physicsBody = std::make_shared<Box2DPhysicsBody>(bodyId, def);
        m_Bodies.insert(physicsBody);
        return physicsBody;
    }

    void Box2DPhysicsWorld::DestroyBody(IPhysicsBody* body) {
        if (!body || !b2World_IsValid(m_WorldId)) return;

        for (auto it = m_Bodies.begin(); it != m_Bodies.end(); ++it) {
            if (it->get() == body) {
                b2BodyId* bodyIdPtr = static_cast<b2BodyId*>(body->GetNativeBody());
                if (bodyIdPtr && b2Body_IsValid(*bodyIdPtr)) {
                    b2DestroyBody(*bodyIdPtr);
                }
                m_Bodies.erase(it);
                return;
            }
        }
    }

    // ============================================================
    // 关节管理
    // ============================================================

    /// 构建 b2Transform（位置 + 零旋转）
    static b2Transform MakeTransform(b2Vec2 pos) {
        b2Transform tf;
        tf.p = pos;
        tf.q = b2MakeRot(0.0f);
        return tf;
    }

    /// 计算轴向量对应的旋转（使 local x 轴指向 axis 方向）
    static b2Rot MakeRotFromAxis(b2Vec2 axis) {
        float angle = atan2f(axis.y, axis.x);
        return b2MakeRot(angle);
    }

    static b2JointId CreateBox2DJoint(b2WorldId worldId, const JointDef& def,
                                       b2BodyId bodyIdA, b2BodyId bodyIdB) {
        if (!b2World_IsValid(worldId) || !b2Body_IsValid(bodyIdA)) return b2JointId{};
        if (def.type != JointType::Mouse && !b2Body_IsValid(bodyIdB)) return b2JointId{};

        switch (def.type) {
            case JointType::Revolute: {
                const auto& rd = static_cast<const RevoluteJointDef&>(def);
                b2RevoluteJointDef jd = b2DefaultRevoluteJointDef();
                jd.base.bodyIdA = bodyIdA;
                jd.base.bodyIdB = bodyIdB;
                jd.base.localFrameA = MakeTransform(ToB2(rd.localAnchorA));
                jd.base.localFrameB = MakeTransform(ToB2(rd.localAnchorB));
                jd.base.collideConnected = def.collideConnected;
                jd.enableLimit  = rd.enableLimit;
                jd.lowerAngle   = rd.lowerAngle;
                jd.upperAngle   = rd.upperAngle;
                jd.enableMotor  = rd.enableMotor;
                jd.motorSpeed   = rd.motorSpeed;
                jd.maxMotorTorque = rd.maxMotorTorque;
                return b2CreateRevoluteJoint(worldId, &jd);
            }
            case JointType::Prismatic: {
                const auto& pd = static_cast<const PrismaticJointDef&>(def);
                b2PrismaticJointDef jd = b2DefaultPrismaticJointDef();
                jd.base.bodyIdA = bodyIdA;
                jd.base.bodyIdB = bodyIdB;
                jd.base.localFrameA = MakeTransform(ToB2(pd.localAnchorA));
                jd.base.localFrameA.q = MakeRotFromAxis(ToB2(pd.localAxisA));
                jd.base.localFrameB = MakeTransform(ToB2(pd.localAnchorB));
                jd.base.collideConnected = def.collideConnected;
                jd.enableLimit  = pd.enableLimit;
                jd.lowerTranslation = pd.lowerTranslation;
                jd.upperTranslation = pd.upperTranslation;
                jd.enableMotor  = pd.enableMotor;
                jd.motorSpeed   = pd.motorSpeed;
                jd.maxMotorForce = pd.maxMotorForce;
                return b2CreatePrismaticJoint(worldId, &jd);
            }
            case JointType::Distance: {
                const auto& dd = static_cast<const DistanceJointDef&>(def);
                b2DistanceJointDef jd = b2DefaultDistanceJointDef();
                jd.base.bodyIdA = bodyIdA;
                jd.base.bodyIdB = bodyIdB;
                jd.base.localFrameA = MakeTransform(ToB2(dd.localAnchorA));
                jd.base.localFrameB = MakeTransform(ToB2(dd.localAnchorB));
                jd.base.collideConnected = def.collideConnected;
                jd.length       = dd.length;
                jd.hertz        = dd.stiffness;
                jd.dampingRatio = dd.damping;
                jd.enableSpring = true;
                return b2CreateDistanceJoint(worldId, &jd);
            }
            case JointType::Weld: {
                const auto& wd = static_cast<const WeldJointDef&>(def);
                b2WeldJointDef jd = b2DefaultWeldJointDef();
                jd.base.bodyIdA = bodyIdA;
                jd.base.bodyIdB = bodyIdB;
                jd.base.localFrameA = MakeTransform(ToB2(wd.localAnchorA));
                jd.base.localFrameB = MakeTransform(ToB2(wd.localAnchorB));
                jd.base.collideConnected = def.collideConnected;
                return b2CreateWeldJoint(worldId, &jd);
            }
            case JointType::Wheel: {
                const auto& wh = static_cast<const WheelJointDef&>(def);
                b2WheelJointDef jd = b2DefaultWheelJointDef();
                jd.base.bodyIdA = bodyIdA;
                jd.base.bodyIdB = bodyIdB;
                jd.base.localFrameA = MakeTransform(ToB2(wh.localAnchorA));
                jd.base.localFrameA.q = MakeRotFromAxis(ToB2(wh.localAxisA));
                jd.base.localFrameB = MakeTransform(ToB2(wh.localAnchorB));
                jd.base.collideConnected = def.collideConnected;
                jd.enableMotor  = wh.enableMotor;
                jd.motorSpeed   = wh.motorSpeed;
                jd.maxMotorTorque = wh.maxMotorTorque;
                jd.hertz        = wh.stiffness;
                jd.dampingRatio = wh.damping;
                return b2CreateWheelJoint(worldId, &jd);
            }
            case JointType::Mouse: {
                const auto& md = static_cast<const MouseJointDef&>(def);
                b2MotorJointDef jd = b2DefaultMotorJointDef();
                jd.base.bodyIdA = bodyIdA;
                jd.base.bodyIdB = bodyIdB;
                jd.base.collideConnected = false;
                jd.linearVelocity  = {0.0f, 0.0f};
                jd.angularVelocity = 0.0f;
                jd.maxVelocityForce  = 0.0f;
                jd.maxVelocityTorque = 0.0f;
                jd.linearHertz       = md.stiffness;
                jd.linearDampingRatio = md.damping;
                jd.maxSpringForce    = md.maxForce;
                jd.maxSpringTorque   = 0.0f;
                return b2CreateMotorJoint(worldId, &jd);
            }
            default:
                return b2JointId{};
        }
    }

    std::shared_ptr<IJoint> Box2DPhysicsWorld::CreateJoint(const JointDef& def) {
        if (!b2World_IsValid(m_WorldId)) return nullptr;
        if (!def.bodyA) return nullptr;
        if (def.type != JointType::Mouse && !def.bodyB) return nullptr;

        b2BodyId* bodyIdAPtr = static_cast<b2BodyId*>(def.bodyA->GetNativeBody());
        b2BodyId* bodyIdBPtr = def.bodyB
            ? static_cast<b2BodyId*>(def.bodyB->GetNativeBody()) : nullptr;

        if (!bodyIdAPtr || !b2Body_IsValid(*bodyIdAPtr)) return nullptr;

        b2BodyId bodyIdB = bodyIdBPtr ? *bodyIdBPtr : *bodyIdAPtr;  // MouseJoint fallback
        b2JointId jointId = CreateBox2DJoint(m_WorldId, def, *bodyIdAPtr, bodyIdB);
        if (!b2Joint_IsValid(jointId)) return nullptr;

        auto jointWrapper = std::make_shared<Box2DJoint>(jointId, def);
        m_Joints.insert(jointWrapper);
        return jointWrapper;
    }

    void Box2DPhysicsWorld::DestroyJoint(IJoint* joint) {
        if (!joint || !b2World_IsValid(m_WorldId)) return;

        for (auto it = m_Joints.begin(); it != m_Joints.end(); ++it) {
            if (it->get() == joint) {
                b2JointId* jointIdPtr = static_cast<b2JointId*>(joint->GetNativeJoint());
                if (jointIdPtr && b2Joint_IsValid(*jointIdPtr)) {
                    b2DestroyJoint(*jointIdPtr, true);
                }
                m_Joints.erase(it);
                return;
            }
        }
    }

    // ============================================================
    // 查询
    // ============================================================

    std::vector<RayCastResult> Box2DPhysicsWorld::RayCast(const Vec2& from,
                                                           const Vec2& to) {
        std::vector<RayCastResult> results;
        if (!b2World_IsValid(m_WorldId)) return results;

        b2QueryFilter filter = b2DefaultQueryFilter();
        b2Vec2 origin = ToB2(from);
        b2Vec2 translation = {ToB2(to).x - origin.x, ToB2(to).y - origin.y};

        // 使用 RayCast 回调
        struct Context {
            std::vector<RayCastResult>& Results;
            b2Vec2 Origin;
        };
        Context ctx{results, origin};

        auto callback = [](b2ShapeId shapeId, b2Vec2 point, b2Vec2 normal,
                           float fraction, void* context) -> float {
            auto* ctx = static_cast<Context*>(context);
            b2BodyId bodyId = b2Shape_GetBody(shapeId);
            IPhysicsBody* body = static_cast<IPhysicsBody*>(b2Body_GetUserData(bodyId));
            if (!body) return fraction;

            RayCastResult result;
            result.body     = body;
            result.point    = FromB2(point);
            result.normal   = FromB2(normal);
            result.fraction = fraction;
            ctx->Results.push_back(result);
            return fraction; // 继续收集
        };

        b2World_CastRay(m_WorldId, origin, translation, filter, callback, &ctx);

        std::sort(results.begin(), results.end(),
            [](const RayCastResult& a, const RayCastResult& b) {
                return a.fraction < b.fraction;
            });

        return results;
    }

    std::vector<IPhysicsBody*> Box2DPhysicsWorld::QueryAABB(const Vec2& center,
                                                              const Vec2& halfSize) {
        std::vector<IPhysicsBody*> results;
        if (!b2World_IsValid(m_WorldId)) return results;

        b2AABB aabb;
        aabb.lowerBound = ToB2(Vec2(center.x - halfSize.x, center.y - halfSize.y));
        aabb.upperBound = ToB2(Vec2(center.x + halfSize.x, center.y + halfSize.y));

        struct Context {
            std::vector<IPhysicsBody*>& Results;
        };
        Context ctx{results};

        auto callback = [](b2ShapeId shapeId, void* context) -> bool {
            auto* ctx = static_cast<Context*>(context);
            b2BodyId bodyId = b2Shape_GetBody(shapeId);
            IPhysicsBody* body = static_cast<IPhysicsBody*>(b2Body_GetUserData(bodyId));
            if (body) {
                if (std::find(ctx->Results.begin(), ctx->Results.end(), body)
                    == ctx->Results.end()) {
                    ctx->Results.push_back(body);
                }
            }
            return true;
        };

        b2World_OverlapAABB(m_WorldId, aabb, b2DefaultQueryFilter(), callback, &ctx);
        return results;
    }

    // ============================================================
    // 重力
    // ============================================================

    void Box2DPhysicsWorld::SetGravity(const Vec2& gravity) {
        if (b2World_IsValid(m_WorldId))
            b2World_SetGravity(m_WorldId, ToB2(gravity));
    }

    Vec2 Box2DPhysicsWorld::GetGravity() const {
        if (!b2World_IsValid(m_WorldId)) return Vec2(0.0f, 0.0f);
        return FromB2(b2World_GetGravity(m_WorldId));
    }

    // ============================================================
    // 碰撞回调注册
    // ============================================================

    void Box2DPhysicsWorld::SetContactBeginCallback(ContactCallback callback) {
        m_ContactBeginCallback = std::move(callback);
    }

    void Box2DPhysicsWorld::SetContactEndCallback(ContactCallback callback) {
        m_ContactEndCallback = std::move(callback);
    }

    void Box2DPhysicsWorld::SetContactPreSolveCallback(ContactFilterCallback callback) {
        m_ContactPreSolveCallback = std::move(callback);
    }

    void Box2DPhysicsWorld::SetContactPersistCallback(ContactPersistCallback callback) {
        m_ContactPersistCallback = std::move(callback);
    }

    // ============================================================
    // 碰撞事件路由
    // ============================================================

    void Box2DPhysicsWorld::OnContactBegin(const ContactInfo& info) {
        if (m_ContactBeginCallback) {
            m_ContactBeginCallback(info);
        }
        RouteContactEvent(info, [](const PhysicsComponent* c, const ContactInfo& i) {
            const_cast<PhysicsComponent*>(c)->InvokeCollisionEnter(i);
        });
    }

    void Box2DPhysicsWorld::OnContactEnd(const ContactInfo& info) {
        if (m_ContactEndCallback) {
            m_ContactEndCallback(info);
        }
        RouteContactEvent(info, [](const PhysicsComponent* c, const ContactInfo& i) {
            const_cast<PhysicsComponent*>(c)->InvokeCollisionExit(i);
        });
    }

    bool Box2DPhysicsWorld::OnContactPreSolve(const void* bodyA, const void* bodyB) {
        if (m_ContactPreSolveCallback) {
            return m_ContactPreSolveCallback(bodyA, bodyB);
        }
        return true;
    }

    void Box2DPhysicsWorld::OnContactPersist(const ContactPersistData& data) {
        if (m_ContactPersistCallback) {
            m_ContactPersistCallback(data);
        }
        RoutePersistEvent(data);
    }

    // ============================================================
    // 调试绘制
    // ============================================================

    void Box2DPhysicsWorld::SetDebugDraw(IPhysicsDebugDraw* draw) {
        m_UserDebugDraw = draw;
    }

    void Box2DPhysicsWorld::DebugDraw() {
        if (!b2World_IsValid(m_WorldId) || !m_UserDebugDraw) return;
        b2DebugDraw draw = Box2DDebugDraw::Create(*m_UserDebugDraw);
        b2World_Draw(m_WorldId, &draw);
    }

    // ============================================================
    // 原生指针
    // ============================================================

    void* Box2DPhysicsWorld::GetNativeWorld() {
        return static_cast<void*>(&m_WorldId);
    }

} // namespace Engine
