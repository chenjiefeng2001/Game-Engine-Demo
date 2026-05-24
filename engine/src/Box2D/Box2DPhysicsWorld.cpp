#include "Box2DPhysicsWorld.h"   // => Engine/Box2D/Box2DPhysicsWorld.h + box2d.h
#include "Box2DPhysicsBody.h"
#include "Box2DJoint.h"
#include "Box2DDebugDraw.h"
#include "Box2DContactListener.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include <algorithm>

namespace Engine {

    // ============================================================
    // 匿名命名空间：引擎类型 ↔ Box2D 类型转换（文件内私有）
    // ============================================================
    namespace {

        b2Vec2 ToB2(const Vec2& v) {
            return b2Vec2(v.x, v.y);
        }

        Vec2 FromB2(const b2Vec2& v) {
            return Vec2(v.x, v.y);
        }

        b2BodyType ToB2Type(BodyType type) {
            switch (type) {
                case BodyType::Static:    return b2_staticBody;
                case BodyType::Dynamic:   return b2_dynamicBody;
                case BodyType::Kinematic: return b2_kinematicBody;
            }
            return b2_staticBody;
        }

        BodyType FromB2Type(b2BodyType type) {
            switch (type) {
                case b2_staticBody:    return BodyType::Static;
                case b2_dynamicBody:   return BodyType::Dynamic;
                case b2_kinematicBody: return BodyType::Kinematic;
            }
            return BodyType::Static;
        }

        /**
         * @brief 将 BodyDef 转换为 Box2D 形状定义并创建夹具
         */
        void CreateFixtureFromDef(b2Body* body, const BodyDef& def) {
            b2FixtureDef fixtureDef;
            fixtureDef.density     = def.density;
            fixtureDef.friction    = def.friction;
            fixtureDef.restitution = def.restitution;

            b2Filter filter;
            filter.categoryBits = def.categoryBits;
            filter.maskBits     = def.maskBits;
            filter.groupIndex   = def.groupIndex;
            fixtureDef.filter    = filter;

            b2PolygonShape boxShape;
            b2CircleShape  circleShape;

            switch (def.shape.type) {
                case ShapeType::Box: {
                    boxShape.SetAsBox(def.shape.boxSize.x, def.shape.boxSize.y,
                                      ToB2(def.shape.offset), 0.0f);
                    fixtureDef.shape = &boxShape;
                    break;
                }
                case ShapeType::Circle: {
                    circleShape.m_radius = def.shape.circleRadius;
                    circleShape.m_p      = ToB2(def.shape.offset);
                    fixtureDef.shape = &circleShape;
                    break;
                }
            }

            body->CreateFixture(&fixtureDef);
        }

    } // anonymous namespace

    // ============================================================
    // 构造 / 析构
    // ============================================================

    Box2DPhysicsWorld::Box2DPhysicsWorld(const Vec2& gravity)
        : m_World(new b2World(ToB2(gravity)))
    {
        m_ContactListener = std::make_unique<Box2DContactListener>(*this);
        m_World->SetContactListener(m_ContactListener.get());
    }

    Box2DPhysicsWorld::~Box2DPhysicsWorld() {
        // 清理所有关节和刚体（顺序重要：关节先于刚体）
        m_Joints.clear();
        m_Bodies.clear();

        delete m_World;
        m_World = nullptr;
    }

    // ============================================================
    // 步进
    // ============================================================

    void Box2DPhysicsWorld::Step(float32 dt,
                                 int32 velocityIterations,
                                 int32 positionIterations) {
        if (m_World) {
            m_World->Step(dt, velocityIterations, positionIterations);
        }
    }

    // ============================================================
    // 刚体管理
    // ============================================================

    std::shared_ptr<IPhysicsBody> Box2DPhysicsWorld::CreateBody(const BodyDef& def) {
        if (!m_World) return nullptr;

        b2BodyDef bd;
        bd.type          = ToB2Type(def.type);  // 使用 Box2DPhysicsBody 中的转换函数
        bd.position      = ToB2(def.position);
        bd.angle         = def.angle;
        bd.linearDamping = def.linearDamping;
        bd.angularDamping= def.angularDamping;
        bd.fixedRotation = def.fixedRotation;
        bd.allowSleep    = def.allowSleep;

        // body 的用户数据会在 Box2DPhysicsBody 构造时设置
        bd.userData.pointer = 0;

        b2Body* body = m_World->CreateBody(&bd);
        if (!body) return nullptr;

        // 创建夹具（形状）
        CreateFixtureFromDef(body, def);

        // 创建包装对象
        auto physicsBody = std::make_shared<Box2DPhysicsBody>(body, def);
        m_Bodies.insert(physicsBody);
        return physicsBody;
    }

    void Box2DPhysicsWorld::DestroyBody(IPhysicsBody* body) {
        if (!body || !m_World) return;

        // 查找对应的 Box2DPhysicsBody
        for (auto it = m_Bodies.begin(); it != m_Bodies.end(); ++it) {
            if (it->get() == body) {
                b2Body* b2BodyPtr = static_cast<b2Body*>(body->GetNativeBody());
                if (b2BodyPtr) {
                    m_World->DestroyBody(b2BodyPtr);
                }
                m_Bodies.erase(it);
                return;
            }
        }
    }

    // ============================================================
    // 关节管理
    // ============================================================

    static b2Joint* CreateBox2DJoint(b2World* world, const JointDef& def,
                                     b2Body* bodyA, b2Body* bodyB) {
        if (!world || !bodyA) return nullptr;
        // MouseJoint 允许 bodyB 为 nullptr
        if (def.type != JointType::Mouse && (!bodyA || !bodyB)) return nullptr;

        switch (def.type) {
            case JointType::Revolute: {
                const auto& rd = static_cast<const RevoluteJointDef&>(def);
                b2RevoluteJointDef jd;
                jd.Initialize(bodyA, bodyB, ToB2(rd.localAnchorA));
                jd.localAnchorB     = ToB2(rd.localAnchorB);
                jd.referenceAngle   = rd.referenceAngle;
                jd.enableLimit      = rd.enableLimit;
                jd.lowerAngle       = rd.lowerAngle;
                jd.upperAngle       = rd.upperAngle;
                jd.enableMotor      = rd.enableMotor;
                jd.motorSpeed       = rd.motorSpeed;
                jd.maxMotorTorque   = rd.maxMotorTorque;
                jd.collideConnected = def.collideConnected;
                return world->CreateJoint(&jd);
            }
            case JointType::Prismatic: {
                const auto& pd = static_cast<const PrismaticJointDef&>(def);
                b2PrismaticJointDef jd;
                jd.Initialize(bodyA, bodyB, ToB2(pd.localAnchorA), ToB2(pd.localAxisA));
                jd.localAnchorB       = ToB2(pd.localAnchorB);
                jd.referenceAngle     = pd.referenceAngle;
                jd.enableLimit        = pd.enableLimit;
                jd.lowerTranslation   = pd.lowerTranslation;
                jd.upperTranslation   = pd.upperTranslation;
                jd.enableMotor        = pd.enableMotor;
                jd.motorSpeed         = pd.motorSpeed;
                jd.maxMotorForce      = pd.maxMotorForce;
                jd.collideConnected   = def.collideConnected;
                return world->CreateJoint(&jd);
            }
            case JointType::Distance: {
                const auto& dd = static_cast<const DistanceJointDef&>(def);
                b2DistanceJointDef jd;
                jd.Initialize(bodyA, bodyB, ToB2(dd.localAnchorA), ToB2(dd.localAnchorB));
                jd.length    = dd.length;
                jd.stiffness = dd.stiffness;
                jd.damping   = dd.damping;
                jd.collideConnected = def.collideConnected;
                return world->CreateJoint(&jd);
            }
            case JointType::Weld: {
                const auto& wd = static_cast<const WeldJointDef&>(def);
                b2WeldJointDef jd;
                jd.Initialize(bodyA, bodyB, ToB2(wd.localAnchorA));
                jd.localAnchorB     = ToB2(wd.localAnchorB);
                jd.referenceAngle   = wd.referenceAngle;
                jd.collideConnected = def.collideConnected;
                return world->CreateJoint(&jd);
            }
            case JointType::Wheel: {
                const auto& wh = static_cast<const WheelJointDef&>(def);
                b2WheelJointDef jd;
                jd.Initialize(bodyA, bodyB, ToB2(wh.localAnchorA), ToB2(wh.localAxisA));
                jd.localAnchorB     = ToB2(wh.localAnchorB);
                jd.enableMotor      = wh.enableMotor;
                jd.motorSpeed       = wh.motorSpeed;
                jd.maxMotorTorque   = wh.maxMotorTorque;
                jd.stiffness        = wh.stiffness;
                jd.damping          = wh.damping;
                jd.collideConnected = def.collideConnected;
                return world->CreateJoint(&jd);
            }
            case JointType::Mouse: {
                const auto& md = static_cast<const MouseJointDef&>(def);
                b2MouseJointDef jd;
                jd.bodyA        = bodyA;
                jd.bodyB        = bodyB ? bodyB : bodyA;  // bodyB 设为自身，目标通过 target 控制
                jd.target       = ToB2(md.target);
                jd.maxForce     = md.maxForce;
                jd.stiffness    = md.stiffness;
                jd.damping      = md.damping;
                jd.collideConnected = false;
                return world->CreateJoint(&jd);
            }
            default:
                return nullptr;
        }
    }

    std::shared_ptr<IJoint> Box2DPhysicsWorld::CreateJoint(const JointDef& def) {
        if (!m_World) return nullptr;
        if (!def.bodyA) return nullptr;
        // MouseJoint 不需要 bodyB
        if (def.type != JointType::Mouse && !def.bodyB) return nullptr;

        // 获取 Box2D 的 b2Body 指针
        b2Body* bodyA = static_cast<b2Body*>(def.bodyA->GetNativeBody());
        b2Body* bodyB = def.bodyB
            ? static_cast<b2Body*>(def.bodyB->GetNativeBody())
            : bodyA;  // MouseJoint: bodyB 指向自身
        if (!bodyA) return nullptr;

        b2Joint* joint = CreateBox2DJoint(m_World, def, bodyA, bodyB);
        if (!joint) return nullptr;

        auto jointWrapper = std::make_shared<Box2DJoint>(joint, def);
        m_Joints.insert(jointWrapper);
        return jointWrapper;
    }

    void Box2DPhysicsWorld::DestroyJoint(IJoint* joint) {
        if (!joint || !m_World) return;

        for (auto it = m_Joints.begin(); it != m_Joints.end(); ++it) {
            if (it->get() == joint) {
                b2Joint* b2JointPtr = static_cast<b2Joint*>(joint->GetNativeJoint());
                if (b2JointPtr) {
                    m_World->DestroyJoint(b2JointPtr);
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

        if (!m_World) return results;

        class RayCastCallback : public b2RayCastCallback {
        public:
            std::vector<RayCastResult>& Results;
            float32 ReportFixture(b2Fixture* fixture, const b2Vec2& point,
                                  const b2Vec2& normal, float32 fraction) override {
                if (!fixture || !fixture->GetBody()) return fraction;

                IPhysicsBody* body = reinterpret_cast<IPhysicsBody*>(
                    fixture->GetBody()->GetUserData().pointer);
                if (!body) return fraction;

                RayCastResult result;
                result.body     = body;
                result.point    = FromB2(point);
                result.normal   = FromB2(normal);
                result.fraction = fraction;
                Results.push_back(result);

                return fraction; // 继续收集所有结果
            }
            explicit RayCastCallback(std::vector<RayCastResult>& r) : Results(r) {}
        };

        RayCastCallback callback(results);
        m_World->RayCast(&callback, ToB2(from), ToB2(to));

        // 按 fraction 排序
        std::sort(results.begin(), results.end(),
            [](const RayCastResult& a, const RayCastResult& b) {
                return a.fraction < b.fraction;
            });

        return results;
    }

    std::vector<IPhysicsBody*> Box2DPhysicsWorld::QueryAABB(const Vec2& center,
                                                              const Vec2& halfSize) {
        std::vector<IPhysicsBody*> results;
        if (!m_World) return results;

        b2AABB aabb;
        aabb.lowerBound = ToB2(Vec2(center.x - halfSize.x, center.y - halfSize.y));
        aabb.upperBound = ToB2(Vec2(center.x + halfSize.x, center.y + halfSize.y));

        class QueryCallback : public b2QueryCallback {
        public:
            std::vector<IPhysicsBody*>& Results;
            bool ReportFixture(b2Fixture* fixture) override {
                if (!fixture || !fixture->GetBody()) return true;
                IPhysicsBody* body = reinterpret_cast<IPhysicsBody*>(
                    fixture->GetBody()->GetUserData().pointer);
                if (body) {
                    // 去重
                    if (std::find(Results.begin(), Results.end(), body) == Results.end()) {
                        Results.push_back(body);
                    }
                }
                return true;
            }
            explicit QueryCallback(std::vector<IPhysicsBody*>& r) : Results(r) {}
        };

        QueryCallback callback(results);
        m_World->QueryAABB(&callback, aabb);
        return results;
    }

    // ============================================================
    // 重力
    // ============================================================

    void Box2DPhysicsWorld::SetGravity(const Vec2& gravity) {
        if (m_World) m_World->SetGravity(ToB2(gravity));
    }

    Vec2 Box2DPhysicsWorld::GetGravity() const {
        return m_World ? FromB2(m_World->GetGravity()) : Vec2(0.0f, 0.0f);
    }

    // ============================================================
    // 碰撞事件
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

    // 内部工具：将碰撞事件路由到 PhysicsComponent 级别
    namespace {
        static void RouteContactEvent(const ContactInfo& info,
                                      void (*invoke)(const PhysicsComponent*, const ContactInfo&)) {
            if (!info.bodyA || !info.bodyB) return;
            auto* bodyA = static_cast<const IPhysicsBody*>(info.bodyA);
            auto* bodyB = static_cast<const IPhysicsBody*>(info.bodyB);
            auto* compA = static_cast<PhysicsComponent*>(bodyA->GetComponentRef());
            auto* compB = static_cast<PhysicsComponent*>(bodyB->GetComponentRef());
            if (compA) invoke(compA, info);
            if (compB) invoke(compB, info);
        }
        static void RoutePersistEvent(const ContactPersistData& data) {
            if (!data.bodyA || !data.bodyB) return;
            auto* bodyA = static_cast<const IPhysicsBody*>(data.bodyA);
            auto* bodyB = static_cast<const IPhysicsBody*>(data.bodyB);
            auto* compA = static_cast<PhysicsComponent*>(bodyA->GetComponentRef());
            auto* compB = static_cast<PhysicsComponent*>(bodyB->GetComponentRef());
            if (compA) compA->InvokeCollisionStay(data);
            if (compB) compB->InvokeCollisionStay(data);
        }
    }

    void Box2DPhysicsWorld::OnContactBegin(const ContactInfo& info) {
        // 全局回掉
        if (m_ContactBeginCallback) {
            m_ContactBeginCallback(info);
        }
        // 路由到 PhysicsComponent
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
        return true; // 默认允许碰撞
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
        if (draw) {
            m_Box2DDebugDraw = std::make_unique<Box2DDebugDraw>(*draw);
            m_World->SetDebugDraw(m_Box2DDebugDraw.get());
        } else {
            m_Box2DDebugDraw.reset();
            m_World->SetDebugDraw(nullptr);
        }
    }

    void Box2DPhysicsWorld::DebugDraw() {
        if (m_World && m_Box2DDebugDraw) {
            // 将 IPhysicsDebugDraw 的 flags 同步到 Box2DDebugDraw
            if (m_UserDebugDraw) {
                m_Box2DDebugDraw->SetFlags(m_UserDebugDraw->GetFlags());
            }
            m_World->DebugDraw();
        }
    }

    // ============================================================
    // 原生指针
    // ============================================================

    void* Box2DPhysicsWorld::GetNativeWorld() {
        return static_cast<void*>(m_World);
    }

} // namespace Engine
