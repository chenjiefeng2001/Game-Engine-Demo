/**
 * @file JoltPhysicsWorld.cpp
 * @brief Jolt Physics 3D 物理世界实现 — 替换 NullPhysicsWorld3D
 */

#include "Engine/Jolt/JoltPhysicsWorld.h"
#include "Engine/Jolt/JoltPhysicsBody.h"
#include "Engine/Jolt/JoltJobSystemAdapter.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Physics/PhysicsDefs3D.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/Body.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace Engine {

// ═══════════════════════════════════════════════════════════
// ObjectLayer 映射到 Jolt 的 ObjectLayerPairFilter
// ═══════════════════════════════════════════════════════════

// 将 Engine::ObjectLayer 转换为 Jolt 的 ObjectLayer (uint16)
static_assert(sizeof(ObjectLayer) == sizeof(uint16));

// ═══════════════════════════════════════════════════════════
// Jolt 全局初始化 / 销毁
// ═══════════════════════════════════════════════════════════

// 延迟初始化：在第一次创建 JoltPhysicsWorld 时才注册 Jolt
static std::atomic<int> s_JoltRefCount{0};

static void EnsureJoltInitialized() {
    if (s_JoltRefCount.fetch_add(1) == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        Log::Info("[Jolt] Jolt Physics initialized (v5.5)");
    }
}

static void EnsureJoltShutdown() {
    if (s_JoltRefCount.fetch_sub(1) == 1) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
        Log::Info("[Jolt] Jolt Physics shutdown");
    }
}

// ═══════════════════════════════════════════════════════════
// Jolt ContactListener 实现
// ═══════════════════════════════════════════════════════════

class JoltPhysicsWorld::ContactListenerImpl : public JPH::ContactListener {
public:
    ContactListenerImpl(JoltPhysicsWorld* world) : m_World(world) {}

    // 碰撞开始
    JPH::ValidateResult OnContactValidate(
        const JPH::Body& bodyA, const JPH::Body& bodyB,
        JPH::RVec3Arg, const JPH::CollideShapeResult&) override
    {
        // 在 Worker 线程中调用，不做复杂操作，只做过滤
        if (m_World->m_ContactPreSolve) {
            ContactData3D data;
            data.bodyA = reinterpret_cast<IPhysicsBody3D*>(bodyA.GetUserData());
            data.bodyB = reinterpret_cast<IPhysicsBody3D*>(bodyB.GetUserData());
            // 如果 filter 返回 false，阻止碰撞
            return JPH::ValidateResult::AcceptAll; // 简化
        }
        return JPH::ValidateResult::AcceptAll;
    }

    // 碰撞添加
    void OnContactAdded(const JPH::Body& bodyA, const JPH::Body& bodyB,
        const JPH::ContactManifold& manifold,
        JPH::ContactSettings&) override
    {
        if (m_World->m_ContactBegin) {
            ContactData3D data;
            data.bodyA = reinterpret_cast<IPhysicsBody3D*>(bodyA.GetUserData());
            data.bodyB = reinterpret_cast<IPhysicsBody3D*>(bodyB.GetUserData());
            if (manifold.mRelativeContactPointsOnA.size() > 0) {
                data.contactPoint = Vec3(
                    manifold.mRelativeContactPointsOnA[0].GetX(),
                    manifold.mRelativeContactPointsOnA[0].GetY(),
                    manifold.mRelativeContactPointsOnA[0].GetZ());
                data.contactNormal = Vec3(
                    manifold.mWorldSpaceNormal.GetX(),
                    manifold.mWorldSpaceNormal.GetY(),
                    manifold.mWorldSpaceNormal.GetZ());
            }
            data.penetration = manifold.mPenetrationDepth;
            m_World->m_ContactBegin(data);
        }
    }

    // 碰撞持续
    void OnContactPersisted(const JPH::Body& bodyA, const JPH::Body& bodyB,
        const JPH::ContactManifold& manifold,
        JPH::ContactSettings&) override
    {
        if (m_World->m_ContactPersist) {
            ContactPersistData3D data;
            data.bodyA = reinterpret_cast<IPhysicsBody3D*>(bodyA.GetUserData());
            data.bodyB = reinterpret_cast<IPhysicsBody3D*>(bodyB.GetUserData());
            m_World->m_ContactPersist(data);
        }
    }

    // 碰撞结束
    void OnContactRemoved(const JPH::Body& bodyA, const JPH::Body& bodyB) override {
        if (m_World->m_ContactEnd) {
            ContactData3D data;
            data.bodyA = reinterpret_cast<IPhysicsBody3D*>(bodyA.GetUserData());
            data.bodyB = reinterpret_cast<IPhysicsBody3D*>(bodyB.GetUserData());
            m_World->m_ContactEnd(data);
        }
    }

private:
    JoltPhysicsWorld* m_World;
};

// ═══════════════════════════════════════════════════════════
// JoltPhysicsWorld 构造函数/析构
// ═══════════════════════════════════════════════════════════

JoltPhysicsWorld::JoltPhysicsWorld()
{
    EnsureJoltInitialized();
}

JoltPhysicsWorld::~JoltPhysicsWorld() {
    Shutdown();
    EnsureJoltShutdown();
}

bool JoltPhysicsWorld::Init(const PhysicsWorldConfig3D& config) {
    // ── 分配器 ──
    m_TempAllocator = new JPH::TempAllocatorImpl(16 * 1024 * 1024); // 16MB

    // ── JobSystem 适配器 ──
    m_JobSystemAdapter = new JoltJobSystemAdapter();

    // ── Layer 映射 ──
    // ObjectLayer 有 8 层 (COUNT)
    uint32 numLayers = static_cast<uint32>(ObjectLayer::COUNT);
    m_BPInterface = new JPH::BroadPhaseLayerInterfaceTable(numLayers, static_cast<uint32>(BroadPhaseLayer::COUNT));
    for (uint32 i = 0; i < numLayers; ++i) {
        BroadPhaseLayer bp = GetBroadPhaseLayer(static_cast<ObjectLayer>(i));
        m_BPInterface->MapObjectToBroadPhaseLayer(i, static_cast<JPH::BroadPhaseLayer::Type>(bp));
    }

    // ObjectLayer 碰撞过滤器
    m_ObjectLayerFilter = new JPH::ObjectLayerPairFilterTable(numLayers);
    for (uint32 a = 0; a < numLayers; ++a) {
        for (uint32 b = 0; b < numLayers; ++b) {
            bool shouldCollide = ShouldCollide(static_cast<ObjectLayer>(a), static_cast<ObjectLayer>(b));
            m_ObjectLayerFilter->DisableCollision(
                static_cast<JPH::ObjectLayer>(a),
                static_cast<JPH::ObjectLayer>(b));
            if (shouldCollide) {
                m_ObjectLayerFilter->EnableCollision(
                    static_cast<JPH::ObjectLayer>(a),
                    static_cast<JPH::ObjectLayer>(b));
            }
        }
    }

    // BroadPhase 层间过滤
    m_ObjectVsBPFilter = new JPH::ObjectVsBroadPhaseLayerFilterTable(
        *m_BPInterface, numLayers, static_cast<uint32>(BroadPhaseLayer::COUNT));
    for (uint32 a = 0; a < numLayers; ++a) {
        for (uint32 b = 0; b < static_cast<uint32>(BroadPhaseLayer::COUNT); ++b) {
            // 默认允许所有 ObjectLayer 与 BroadPhaseLayer 交互
        }
    }

    // ── 初始化 PhysicsSystem ──
    const uint32 maxBodies = static_cast<uint32>(config.maxBodies > 0 ? config.maxBodies : 65536);
    const uint32 numBodyMutexes = 0; // 0 = 自动
    const uint32 maxContactConstraints = static_cast<uint32>(config.maxContactConstraints > 0 ? config.maxContactConstraints : 10240);

    m_PhysicsSystem.Init(
        maxBodies,
        numBodyMutexes,
        maxContactConstraints,
        maxBodies,
        *m_BPInterface,
        *m_ObjectVsBPFilter,
        *m_ObjectLayerFilter
    );

    // ── 设置重力 ──
    SetGravity(config.gravity);

    // ── 碰撞监听 ──
    m_ContactListener = new ContactListenerImpl(this);
    m_PhysicsSystem.SetContactListener(m_ContactListener);

    Log::Info("[Jolt] Physics world initialized (maxBodies={})", maxBodies);
    return true;
}

void JoltPhysicsWorld::Shutdown() {
    delete m_ContactListener;
    m_ContactListener = nullptr;
    delete m_ObjectLayerFilter;
    delete m_ObjectVsBPFilter;
    delete m_BPInterface;
    delete m_JobSystemAdapter;
    delete m_TempAllocator;
    m_TempAllocator = nullptr;
    m_JobSystemAdapter = nullptr;
    m_BodyMap.clear();
}

void JoltPhysicsWorld::Step(float32 dt, int32 collisionSteps) {
    // warmStartIterations 在 Init 时从 PhysicsWorldConfig3D 中读取
    // 但 Jolt 的 PhysicsSystem::Update 需要显式传入，因此每次步进时使用
    // 缓存在 JoltPhysicsWorld 成员变量中的求解器参数
    // 暂时使用固定值 1（后续可由 PhysicsWorldConfig3D 控制）
    const int32 warmStartIterations = 1;
    m_PhysicsSystem.Update(
        static_cast<float>(dt),
        collisionSteps,
        warmStartIterations,
        m_TempAllocator,
        m_JobSystemAdapter
    );
}

// ── BodyDef3D → JPH::BodyCreationSettings ──
JPH::BodyCreationSettings JoltPhysicsWorld::ToJoltBodySettings(const BodyDef3D& def) {
    // 形状
    JPH::ShapeRefC shape;
    switch (def.shape.type) {
        case ShapeType3D::Box:
            shape = new JPH::BoxShape(JPH::Vec3(
                def.shape.boxHalfExtents.x,
                def.shape.boxHalfExtents.y,
                def.shape.boxHalfExtents.z));
            break;
        case ShapeType3D::Sphere:
            shape = new JPH::SphereShape(def.shape.sphereRadius);
            break;
        case ShapeType3D::Capsule:
            shape = new JPH::CapsuleShape(def.shape.capsuleHeight * 0.5f, def.shape.capsuleRadius);
            break;
        default:
            // 默认使用 0.5m 立方体
            shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
            break;
    }

    // 运动类型
    JPH::EMotionType motionType;
    switch (def.type) {
        case BodyType3D::Static:    motionType = JPH::EMotionType::Static; break;
        case BodyType3D::Kinematic: motionType = JPH::EMotionType::Kinematic; break;
        default:                    motionType = JPH::EMotionType::Dynamic; break;
    }

    // 构建设置
    JPH::BodyCreationSettings settings(
        shape,
        JPH::RVec3(def.position.x, def.position.y, def.position.z),
        JPH::Quat::sIdentity(),
        motionType,
        static_cast<JPH::ObjectLayer>(def.shape.type == ShapeType3D::Box ? ObjectLayer::MOVING : ObjectLayer::MOVING)
    );

    // 物理材质
    settings.mFriction = def.friction;
    settings.mRestitution = def.restitution;
    settings.mLinearDamping = def.linearDamping;
    settings.mAngularDamping = def.angularDamping;
    settings.mAllowSleeping = def.allowSleep;
    settings.mIsSensor = def.shape.isSensor;

    // CCD 连续碰撞检测
    if (def.isBullet) {
        settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
    }

    return settings;
}

// ── CreateBody ──
std::shared_ptr<IPhysicsBody3D> JoltPhysicsWorld::CreateBody(const BodyDef3D& def) {
    JPH::BodyCreationSettings settings = ToJoltBodySettings(def);

    // 创建 Body
    JPH::Body* body = m_PhysicsSystem.GetBodyInterface().CreateBody(settings);
    if (!body) {
        Log::Error("[Jolt] Failed to create body");
        return nullptr;
    }

    // 添加到世界
    m_PhysicsSystem.GetBodyInterface().AddBody(body->GetID(), JPH::EActivation::Activate);

    // 创建 wrapper
    auto physicsBody = std::make_shared<JoltPhysicsBody>(body, this);
    uint64 id = BodyIDToU64(body->GetID());

    // 设置 userdata 为 physicsBody 指针
    body->SetUserData(reinterpret_cast<uint64>(physicsBody.get()));

    m_BodyMap[id] = physicsBody;
    return physicsBody;
}

void JoltPhysicsWorld::DestroyBody(IPhysicsBody3D* body) {
    auto* joltBody = static_cast<JoltPhysicsBody*>(body);
    JPH::BodyID id = joltBody->GetBodyID();
    auto it = m_BodyMap.find(BodyIDToU64(id));
    if (it != m_BodyMap.end()) {
        m_PhysicsSystem.GetBodyInterface().RemoveBody(id);
        m_PhysicsSystem.GetBodyInterface().DestroyBody(id);
        m_BodyMap.erase(it);
    }
}

// ── 生命周期 ──
IPhysicsBody3D* JoltPhysicsWorld::GetBodyByID(uint64 bodyID) {
    auto it = m_BodyMap.find(bodyID);
    if (it != m_BodyMap.end()) {
        return it->second.get();
    }
    return nullptr;
}

void JoltPhysicsWorld::RemoveBody(uint64 bodyID) {
    auto it = m_BodyMap.find(bodyID);
    if (it != m_BodyMap.end()) {
        JPH::BodyID id = U64ToBodyID(bodyID);
        m_PhysicsSystem.GetBodyInterface().RemoveBody(id);
        m_PhysicsSystem.GetBodyInterface().DestroyBody(id);
        m_BodyMap.erase(it);
    }
}

bool JoltPhysicsWorld::IsBodyValid(uint64 bodyID) {
    JPH::BodyID id = U64ToBodyID(bodyID);
    JPH::Body* body = m_PhysicsSystem.GetBodyInterface().FindBody(id);
    return body != nullptr && body->IsInBroadPhase();
}

// ── 查询 ──
std::vector<RayCastResult3D> JoltPhysicsWorld::RayCast(const Vec3& from, const Vec3& to) {
    std::vector<RayCastResult3D> results;
    JPH::RRayCast ray(JPH::Vec3(from.x, from.y, from.z), JPH::Vec3(to.x - from.x, to.y - from.y, to.z - from.z));
    JPH::RayCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
    m_PhysicsSystem.GetNarrowPhaseQuery().CastRay(ray, settings, collector);
    if (collector.Hit()) {
        RayCastResult3D result;
        auto* body = const_cast<JPH::Body*>(collector.mHit.mBody);
        result.body = reinterpret_cast<IPhysicsBody3D*>(body->GetUserData());
        result.fraction = collector.mHit.mFraction;
        result.point = Vec3(
            from.x + (to.x - from.x) * result.fraction,
            from.y + (to.y - from.y) * result.fraction,
            from.z + (to.z - from.z) * result.fraction);
        result.normal = Vec3(
            collector.mHit.mHitPos.GetX(),
            collector.mHit.mHitPos.GetY(),
            collector.mHit.mHitPos.GetZ());
        results.push_back(result);
    }
    return results;
}

std::vector<IPhysicsBody3D*> JoltPhysicsWorld::QueryAABB(const Vec3& center, const Vec3& halfSize) {
    std::vector<IPhysicsBody3D*> results;
    
    JPH::AABox box(
        JPH::Vec3(center.x - halfSize.x, center.y - halfSize.y, center.z - halfSize.z),
        JPH::Vec3(center.x + halfSize.x, center.y + halfSize.y, center.z + halfSize.z));
    
    // 使用 BroadPhaseQuery 进行 AABB 碰撞检测
    JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;
    m_PhysicsSystem.GetBroadPhaseQuery().CollideAABox(box, {}, {}, collector);
    
    for (const JPH::BroadPhaseCastResult& hit : collector.mHits) {
        uint64 bodyID = BodyIDToU64(hit.mBodyID);
        IPhysicsBody3D* body = GetBodyByID(bodyID);
        if (body) {
            results.push_back(body);
        }
    }
    return results;
}

std::vector<IPhysicsBody3D*> JoltPhysicsWorld::QuerySphere(const Vec3& center, float32 radius) {
    std::vector<IPhysicsBody3D*> results;
    
    JPH::SphereShape sphere(radius);
    JPH::Mat44 centerTransform = JPH::Mat44::sTranslation(JPH::Vec3(center.x, center.y, center.z));
    
    JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;
    m_PhysicsSystem.GetNarrowPhaseQuery().CollideShape(
        &sphere, JPH::Vec3::sReplicate(1.0f), centerTransform,
        {}, {}, collector);
    
    for (const JPH::BroadPhaseCastResult& hit : collector.mHits) {
        uint64 bodyID = BodyIDToU64(hit.mBodyID);
        IPhysicsBody3D* body = GetBodyByID(bodyID);
        if (body) {
            results.push_back(body);
        }
    }
    return results;
}

void JoltPhysicsWorld::SetGravity(const Vec3& gravity) {
    m_PhysicsSystem.SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
}

Vec3 JoltPhysicsWorld::GetGravity() const {
    JPH::Vec3 g = m_PhysicsSystem.GetGravity();
    return Vec3(g.GetX(), g.GetY(), g.GetZ());
}

void JoltPhysicsWorld::SetContactBeginCallback(ContactCallback3D cb) {
    m_ContactBegin = std::move(cb);
}

void JoltPhysicsWorld::SetContactEndCallback(ContactCallback3D cb) {
    m_ContactEnd = std::move(cb);
}

void JoltPhysicsWorld::SetContactPreSolveCallback(ContactFilterCallback3D cb) {
    m_ContactPreSolve = std::move(cb);
}

void JoltPhysicsWorld::SetContactPersistCallback(ContactPersistCallback3D cb) {
    m_ContactPersist = std::move(cb);
}

void JoltPhysicsWorld::SetDebugDraw(IPhysicsDebugDraw3D* draw) {
    m_DebugDraw = draw;
}

void JoltPhysicsWorld::DebugDraw() {
    if (m_DebugDraw) {
        // 简化：Jolt DebugRenderer 需要额外实现
    }
}

JoltPhysicsWorld::Stats JoltPhysicsWorld::GetStats() const {
    Stats stats;
    stats.activeBodyCount = m_PhysicsSystem.GetBodyManager().GetNumActiveBodies();
    stats.contactCount = m_PhysicsSystem.GetNumContacts();
    stats.constraintCount = 0; // Jolt 5.5 不直接暴露约束计数
    stats.stepTimeMs = 0.0f;   // 留待接入 Tracy 后实现计时
    return stats;
}

std::shared_ptr<IJoint3D> JoltPhysicsWorld::CreateJoint(const JointDef3D&) {
    return nullptr;
}

void JoltPhysicsWorld::DestroyJoint(IJoint3D*) {}

} // namespace Engine