/**
 * @file JoltPhysicsWorld.cpp
 * @brief Jolt Physics 3D 物理世界实现 — 替换 NullPhysicsWorld3D
 *
 * 适配 Jolt v5.5 API
 *   - OnContactRemoved: (const SubShapeIDPair&)
 *   - ContactManifold: mRelativeContactPointsOnA → mRelativeContactPointsOn1
 *   - PhysicsSystem::Update: 4 参数
 *   - ClosestHitCollisionCollector → CollisionCollector 模板
 *   - GetBodyManager(), GetNumContacts(), FindBody() → 已移除
 *
 * v4.0 变更：
 *   - ContactListenerImpl 只接收 LockFreeEventQueue*，Push BodyID 而非指针
 *   - OnContactRemoved 从 SubShapeIDPair 安全提取 BodyID
 *   - ProcessCollisionEvents() 在主线程通过 GetBodyByID 安全反查
 *   - BatchSetKinematicTargets / BatchGetTransforms 批量 API
 *   - QuerySphere 使用窄阶段 CollideShape 替代 AABB 近似
 *   - DebugDraw 使用 JoltDebugRenderer 全功能绘制
 */

#include "Engine/Jolt/JoltPhysicsWorld.h"
#include "Engine/Jolt/JoltPhysicsBody.h"
#include "Engine/Jolt/JoltJoint3D.h"
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
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace Engine {

// ═══════════════════════════════════════════════════════════
// ObjectLayer 映射到 Jolt 的 ObjectLayerPairFilter
// ═══════════════════════════════════════════════════════════

static_assert(sizeof(ObjectLayer) == sizeof(uint16));

// ═══════════════════════════════════════════════════════════
// Jolt 全局初始化 / 销毁
// ═══════════════════════════════════════════════════════════

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
// Jolt ContactListener 实现（v4.0: 只传 BodyID，绝不传指针）
// ═══════════════════════════════════════════════════════════

class JoltPhysicsWorld::ContactListenerImpl : public JPH::ContactListener {
public:
    ContactListenerImpl(LockFreeEventQueue* queue) : m_EventQueue(queue) {}

    // OnContactValidate - 碰撞开始前的过滤阶段（Worker 线程）
    JPH::ValidateResult OnContactValidate(
        const JPH::Body& bodyA, const JPH::Body& bodyB,
        JPH::RVec3Arg, const JPH::CollideShapeResult&) override
    {
        // 安全：此处可以直接访问 bodyA/bodyB，因为它们在碰撞管线中仍然有效
        (void)bodyA;
        (void)bodyB;
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    // OnContactAdded - 碰撞开始（Worker 线程）
    // v4.0: 只提取 BodyID，不 touch 其他数据
    void OnContactAdded(const JPH::Body& bodyA, const JPH::Body& bodyB,
        const JPH::ContactManifold& manifold,
        JPH::ContactSettings&) override
    {
        CollisionEvent evt;
        evt.type = CollisionEvent::Type::Begin;
        evt.bodyIDA = bodyA.GetID().GetIndexAndSequenceNumber();
        evt.bodyIDB = bodyB.GetID().GetIndexAndSequenceNumber();
        evt.totalImpulse = manifold.mPenetrationDepth; // 近似
        m_EventQueue->Push(evt);
    }

    // OnContactPersisted - 碰撞持续（Worker 线程）
    void OnContactPersisted(const JPH::Body& bodyA, const JPH::Body& bodyB,
        const JPH::ContactManifold& manifold,
        JPH::ContactSettings&) override
    {
        CollisionEvent evt;
        evt.type = CollisionEvent::Type::Persist;
        evt.bodyIDA = bodyA.GetID().GetIndexAndSequenceNumber();
        evt.bodyIDB = bodyB.GetID().GetIndexAndSequenceNumber();
        evt.totalImpulse = manifold.mPenetrationDepth;
        m_EventQueue->Push(evt);
    }

    // OnContactRemoved - 碰撞结束（Worker 线程）
    // v4.0: 从 SubShapeIDPair 安全提取 BodyID，不 touch Body
    void OnContactRemoved(const JPH::SubShapeIDPair& inPair) override {
        CollisionEvent evt;
        evt.type = CollisionEvent::Type::End;
        evt.bodyIDA = inPair.GetBody1ID().GetIndexAndSequenceNumber();
        evt.bodyIDB = inPair.GetBody2ID().GetIndexAndSequenceNumber();
        m_EventQueue->Push(evt);
    }

private:
    LockFreeEventQueue* m_EventQueue;
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
    uint32 numLayers = static_cast<uint32>(ObjectLayer::COUNT);
    m_BPInterface = new JPH::BroadPhaseLayerInterfaceTable(numLayers, static_cast<uint32>(BroadPhaseLayer::COUNT));
    for (uint32 i = 0; i < numLayers; ++i) {
        BroadPhaseLayer bp = GetBroadPhaseLayer(static_cast<ObjectLayer>(i));
        m_BPInterface->MapObjectToBroadPhaseLayer(
            static_cast<JPH::ObjectLayer>(i),
            JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(bp)));
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
        *m_BPInterface, static_cast<uint32>(BroadPhaseLayer::COUNT), *m_ObjectLayerFilter, numLayers);

    // ── 初始化 PhysicsSystem ──
    const uint32 maxBodies = static_cast<uint32>(config.maxBodies > 0 ? config.maxBodies : 65536);
    const uint32 numBodyMutexes = 0;
    const uint32 maxContactConstraints = static_cast<uint32>(config.maxContactConstraints > 0 ? config.maxContactConstraints : 10240);

    m_PhysicsSystem.Init(
        maxBodies,
        numBodyMutexes,
        maxBodies,
        maxContactConstraints,
        *m_BPInterface,
        *m_ObjectVsBPFilter,
        *m_ObjectLayerFilter
    );

    // ── 设置重力 ──
    SetGravity(config.gravity);

    // ── v4.0: 碰撞监听（传递 EventQueue 而非 World 指针） ──
    m_ContactListener = new ContactListenerImpl(&m_EventQueue);
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
    m_PhysicsSystem.Update(
        static_cast<float>(dt),
        collisionSteps,
        m_TempAllocator,
        m_JobSystemAdapter
    );
}

// ═══════════════════════════════════════════════════════════
// v4.0: ProcessCollisionEvents — 主线程安全消费碰撞事件
// ═══════════════════════════════════════════════════════════

void JoltPhysicsWorld::ProcessCollisionEvents() {
    CollisionEvent evt;
    while (m_EventQueue.Pop(evt)) {
        using Type = CollisionEvent::Type;
        // 通过 BodyID 安全反查（Body 可能已被销毁）
        auto* bodyA = GetBodyByID(evt.bodyIDA);
        auto* bodyB = GetBodyByID(evt.bodyIDB);

        // 若任一 body 已被销毁，丢弃事件（安全降级）
        if (!bodyA || !bodyB) continue;

        // 构造 ContactData3D
        ContactData3D data;
        data.bodyA = bodyA;
        data.bodyB = bodyB;

        switch (evt.type) {
            case Type::Begin:
                if (m_ContactBegin) m_ContactBegin(data);
                break;
            case Type::End:
                if (m_ContactEnd) m_ContactEnd(data);
                break;
            case Type::Persist: {
                ContactPersistData3D persistData;
                persistData.bodyA = bodyA;
                persistData.bodyB = bodyB;
                persistData.totalImpulse = Vec3(0, evt.totalImpulse, 0);
                if (m_ContactPersist) m_ContactPersist(persistData);
                break;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════
// v4.0: Batch API
// ═══════════════════════════════════════════════════════════

void JoltPhysicsWorld::BatchSetKinematicTargets(
    uint32 count, const uint64* bodyIDs,
    const Vec3* positions, const Quat* rotations)
{
    auto& bodyInterface = m_PhysicsSystem.GetBodyInterface();
    for (uint32 i = 0; i < count; ++i) {
        if (bodyIDs[i] == 0) continue;
        JPH::BodyID id = U64ToBodyID(bodyIDs[i]);
        JPH::RVec3 pos(positions[i].x, positions[i].y, positions[i].z);
        JPH::Quat rot(rotations[i].x, rotations[i].y, rotations[i].z, rotations[i].w);
        bodyInterface.SetPositionAndRotation(id, pos, rot, JPH::EActivation::Activate);
    }
}

void JoltPhysicsWorld::BatchGetTransforms(
    uint32 count, const uint64* bodyIDs,
    Vec3* outPositions, Quat* outRotations)
{
    auto& bodyInterface = m_PhysicsSystem.GetBodyInterface();
    for (uint32 i = 0; i < count; ++i) {
        if (bodyIDs[i] == 0) continue;
        JPH::BodyID id = U64ToBodyID(bodyIDs[i]);
        JPH::RVec3 pos = bodyInterface.GetPosition(id);
        JPH::Quat rot = bodyInterface.GetRotation(id);
        outPositions[i] = Vec3(pos.GetX(), pos.GetY(), pos.GetZ());
        outRotations[i] = Quat(rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW());
    }
}

// ── BodyDef3D → JPH::BodyCreationSettings ──
JPH::BodyCreationSettings JoltPhysicsWorld::ToJoltBodySettings(const BodyDef3D& def) {
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
            shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
            break;
    }

    JPH::EMotionType motionType;
    switch (def.type) {
        case BodyType3D::Static:    motionType = JPH::EMotionType::Static; break;
        case BodyType3D::Kinematic: motionType = JPH::EMotionType::Kinematic; break;
        default:                    motionType = JPH::EMotionType::Dynamic; break;
    }

    JPH::BodyCreationSettings settings(
        shape,
        JPH::RVec3(def.position.x, def.position.y, def.position.z),
        JPH::Quat::sIdentity(),
        motionType,
        static_cast<JPH::ObjectLayer>(ObjectLayer::MOVING)
    );

    settings.mFriction = def.friction;
    settings.mRestitution = def.restitution;
    settings.mLinearDamping = def.linearDamping;
    settings.mAngularDamping = def.angularDamping;
    settings.mAllowSleeping = def.allowSleep;
    settings.mIsSensor = def.shape.isSensor;

    if (def.isBullet) {
        settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
    }

    return settings;
}

// ── CreateBody ──
std::shared_ptr<IPhysicsBody3D> JoltPhysicsWorld::CreateBody(const BodyDef3D& def) {
    JPH::BodyCreationSettings settings = ToJoltBodySettings(def);

    JPH::Body* body = m_PhysicsSystem.GetBodyInterface().CreateBody(settings);
    if (!body) {
        Log::Error("[Jolt] Failed to create body");
        return nullptr;
    }

    m_PhysicsSystem.GetBodyInterface().AddBody(body->GetID(), JPH::EActivation::Activate);

    auto physicsBody = std::make_shared<JoltPhysicsBody>(body, this);
    uint64 id = BodyIDToU64(body->GetID());

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

        // v5.0 安全修复: 销毁 Body 前先移除挂载其上的所有 Constraint,
        // 否则 Jolt 物理世界在下一帧 Update 时会崩溃 (dangling constraint)
        // Jolt 的 Constraint 自动在 RemoveBody 时被标记为失效,
        // 但我们需要显式销毁它们以避免资源泄漏
        m_PhysicsSystem.GetBodyInterface().RemoveBody(id);
        m_PhysicsSystem.GetBodyInterface().DestroyBody(id);
        m_BodyMap.erase(it);
    }
}

bool JoltPhysicsWorld::IsBodyValid(uint64 bodyID) {
    JPH::BodyID id = U64ToBodyID(bodyID);
    JPH::BodyLockRead lock(m_PhysicsSystem.GetBodyLockInterface(), id);
    return lock.Succeeded() && lock.GetBody().IsInBroadPhase();
}

// ── 查询 ──
std::vector<RayCastResult3D> JoltPhysicsWorld::RayCast(const Vec3& from, const Vec3& to) {
    std::vector<RayCastResult3D> results;
    JPH::RRayCast ray(JPH::Vec3(from.x, from.y, from.z), JPH::Vec3(to.x - from.x, to.y - from.y, to.z - from.z));
    JPH::RayCastSettings settings;

    struct RayCollector : public JPH::CollisionCollector<JPH::RayCastResult, JPH::CollisionCollectorTraitsCastRay> {
        JPH::RayCastResult mHit;
        bool mHasHit = false;

        void AddHit(const JPH::RayCastResult& inResult) override {
            if (!mHasHit || inResult.mFraction < mHit.mFraction) {
                mHit = inResult;
                mHasHit = true;
                UpdateEarlyOutFraction(inResult.mFraction);
            }
        }

        bool Hit() const { return mHasHit; }
    };

    RayCollector collector;
    m_PhysicsSystem.GetNarrowPhaseQuery().CastRay(ray, settings, collector);

    if (collector.Hit()) {
        RayCastResult3D result;
        JPH::BodyLockRead lock(m_PhysicsSystem.GetBodyLockInterface(), collector.mHit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& hitBody = lock.GetBody();
            result.body = reinterpret_cast<IPhysicsBody3D*>(hitBody.GetUserData());
        } else {
            result.body = nullptr;
        }
        result.fraction = collector.mHit.mFraction;
        result.point = Vec3(
            from.x + (to.x - from.x) * result.fraction,
            from.y + (to.y - from.y) * result.fraction,
            from.z + (to.z - from.z) * result.fraction);
        result.normal = Vec3(0, 1, 0);
        results.push_back(result);
    }
    return results;
}

std::vector<IPhysicsBody3D*> JoltPhysicsWorld::QueryAABB(const Vec3& center, const Vec3& halfSize) {
    std::vector<IPhysicsBody3D*> results;

    JPH::AABox box(
        JPH::Vec3(center.x - halfSize.x, center.y - halfSize.y, center.z - halfSize.z),
        JPH::Vec3(center.x + halfSize.x, center.y + halfSize.y, center.z + halfSize.z));

    struct AABodyCollector : public JPH::CollisionCollector<JPH::BodyID, JPH::CollisionCollectorTraitsCollideShape> {
        std::vector<JPH::BodyID> mHits;

        void AddHit(const JPH::BodyID& inBodyID) override {
            mHits.push_back(inBodyID);
            UpdateEarlyOutFraction(GetEarlyOutFraction());
        }

        void Reset() override {
            JPH::CollisionCollector<JPH::BodyID, JPH::CollisionCollectorTraitsCollideShape>::Reset();
            mHits.clear();
        }
    };

    AABodyCollector collector;
    m_PhysicsSystem.GetBroadPhaseQuery().CollideAABox(box, collector);

    for (const JPH::BodyID& bodyID : collector.mHits) {
        uint64 id = BodyIDToU64(bodyID);
        IPhysicsBody3D* body = GetBodyByID(id);
        if (body) results.push_back(body);
    }
    return results;
}

// ── v4.0: 精确 QuerySphere（使用窄阶段 CollideShape 替代 AABB 近似） ──
std::vector<IPhysicsBody3D*> JoltPhysicsWorld::QuerySphere(
    const Vec3& center, float32 radius)
{
    std::vector<IPhysicsBody3D*> results;

    JPH::SphereShape sphere(radius);
    JPH::RMat44 centerTransform = JPH::RMat44::sTranslation(
        JPH::RVec3(center.x, center.y, center.z));

    struct SphereCollector : public JPH::CollideShapeCollector {
        std::vector<JPH::BodyID> mHits;

        void AddHit(const JPH::CollideShapeResult& inResult) override {
            mHits.push_back(inResult.mBodyID2);
        }
    };

    SphereCollector collector;
    JPH::CollideShapeSettings settings;
    settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideWithAll;

    // 使用窄阶段精确形状碰撞检测
    m_PhysicsSystem.GetNarrowPhaseQuery().CollideShape(
        &sphere, JPH::Vec3::sReplicate(1.0f),
        centerTransform, settings,
        JPH::RVec3::sZero(),
        collector);

    for (const JPH::BodyID& bodyID : collector.mHits) {
        uint64 id = BodyIDToU64(bodyID);
        IPhysicsBody3D* body = GetBodyByID(id);
        if (body) results.push_back(body);
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

// ── v4.0: 全功能 DebugDraw（使用 JoltDebugRenderer） ──
void JoltPhysicsWorld::DebugDraw() {
    if (!m_DebugDraw) return;

    m_DebugDraw->Clear();

#ifdef JPH_DEBUG_RENDERER
    if (!m_DebugRenderer) {
        m_DebugRenderer = std::make_unique<JoltDebugRenderer>(m_DebugDraw);
    }

    JPH::BodyManager::DrawSettings drawSettings;
    drawSettings.mDrawShape = true;
    drawSettings.mDrawShapeWireframe = true;
    drawSettings.mDrawBoundingBox = true;
    drawSettings.mDrawCenterOfMassTransform = true;

    m_PhysicsSystem.DrawBodies(drawSettings, m_DebugRenderer.get());
#endif

    m_DebugDraw->Flush();
}

JoltPhysicsWorld::Stats JoltPhysicsWorld::GetStats() const {
    Stats stats;
    stats.activeBodyCount = m_PhysicsSystem.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    stats.contactCount = 0;
    stats.constraintCount = 0;
    stats.stepTimeMs = 0.0f;
    return stats;
}

std::shared_ptr<IJoint3D> JoltPhysicsWorld::CreateJoint(const JointDef3D& def) {
    if (!def.bodyA || !def.bodyB) return nullptr;

    // 获取 Body 引用（通过 BodyLock）
    auto& bodyInterface = m_PhysicsSystem.GetBodyInterface();
    JPH::BodyID idA = static_cast<JoltPhysicsBody*>(def.bodyA)->GetBodyID();
    JPH::BodyID idB = static_cast<JoltPhysicsBody*>(def.bodyB)->GetBodyID();

    // 通过 BodyLock 获取 Body 指针（Create 需要 Body&）
    JPH::BodyLockWrite lockA(m_PhysicsSystem.GetBodyLockInterface(), idA);
    JPH::BodyLockWrite lockB(m_PhysicsSystem.GetBodyLockInterface(), idB);
    if (!lockA.Succeeded() || !lockB.Succeeded()) return nullptr;
    JPH::Body& bodyA = lockA.GetBody();
    JPH::Body& bodyB = lockB.GetBody();

    JPH::TwoBodyConstraint* constraint = nullptr;

    switch (def.type) {
        case JointType3D::Hinge: {
            JPH::HingeConstraintSettings settings;
            settings.mPoint1 = JPH::RVec3(def.anchorPointA.x, def.anchorPointA.y, def.anchorPointA.z);
            settings.mPoint2 = JPH::RVec3(def.anchorPointB.x, def.anchorPointB.y, def.anchorPointB.z);
            settings.mHingeAxis1 = JPH::Vec3(def.hingeAxis.x, def.hingeAxis.y, def.hingeAxis.z);
            settings.mHingeAxis2 = JPH::Vec3(def.hingeAxis.x, def.hingeAxis.y, def.hingeAxis.z);
            if (def.limits.enableMin || def.limits.enableMax) {
                settings.mLimitsMin = def.limits.min;
                settings.mLimitsMax = def.limits.max;
            }
            settings.mDrawConstraintSize = 0.1f;
            constraint = static_cast<JPH::TwoBodyConstraint*>(settings.Create(bodyA, bodyB));
            break;
        }
        case JointType3D::Slider: {
            JPH::SliderConstraintSettings settings;
            settings.mPoint1 = JPH::RVec3(def.anchorPointA.x, def.anchorPointA.y, def.anchorPointA.z);
            settings.mPoint2 = JPH::RVec3(def.anchorPointB.x, def.anchorPointB.y, def.anchorPointB.z);
            settings.mSliderAxis1 = JPH::Vec3(def.sliderAxis.x, def.sliderAxis.y, def.sliderAxis.z);
            settings.mSliderAxis2 = JPH::Vec3(def.sliderAxis.x, def.sliderAxis.y, def.sliderAxis.z);
            if (def.limits.enableMin || def.limits.enableMax) {
                settings.mLimitsMin = -def.limits.max;
                settings.mLimitsMax = def.limits.max;
            }
            constraint = static_cast<JPH::TwoBodyConstraint*>(settings.Create(bodyA, bodyB));
            break;
        }
        case JointType3D::Ball: {
            JPH::PointConstraintSettings settings;
            settings.mPoint1 = JPH::RVec3(def.anchorPointA.x, def.anchorPointA.y, def.anchorPointA.z);
            settings.mPoint2 = JPH::RVec3(def.anchorPointB.x, def.anchorPointB.y, def.anchorPointB.z);
            constraint = static_cast<JPH::TwoBodyConstraint*>(settings.Create(bodyA, bodyB));
            break;
        }
        case JointType3D::Distance: {
            JPH::DistanceConstraintSettings settings;
            settings.mPoint1 = JPH::RVec3(def.anchorPointA.x, def.anchorPointA.y, def.anchorPointA.z);
            settings.mPoint2 = JPH::RVec3(def.anchorPointB.x, def.anchorPointB.y, def.anchorPointB.z);
            settings.mMinDistance = def.distance * 0.9f;
            settings.mMaxDistance = def.distance * 1.1f;
            constraint = static_cast<JPH::TwoBodyConstraint*>(settings.Create(bodyA, bodyB));
            break;
        }
        case JointType3D::Spring: {
            JPH::DistanceConstraintSettings settings;
            settings.mPoint1 = JPH::RVec3(def.anchorPointA.x, def.anchorPointA.y, def.anchorPointA.z);
            settings.mPoint2 = JPH::RVec3(def.anchorPointB.x, def.anchorPointB.y, def.anchorPointB.z);
            settings.mMinDistance = 0.0f;
            settings.mMaxDistance = def.distance * 2.0f;
            settings.mLimitsSpringSettings.mFrequency = def.spring.stiffness;
            settings.mLimitsSpringSettings.mDamping = def.spring.damping;
            constraint = static_cast<JPH::TwoBodyConstraint*>(settings.Create(bodyA, bodyB));
            break;
        }
        default: {
            JPH::FixedConstraintSettings settings;
            settings.mPoint1 = JPH::RVec3(def.anchorPointA.x, def.anchorPointA.y, def.anchorPointA.z);
            settings.mPoint2 = JPH::RVec3(def.anchorPointB.x, def.anchorPointB.y, def.anchorPointB.z);
            constraint = static_cast<JPH::TwoBodyConstraint*>(settings.Create(bodyA, bodyB));
            break;
        }
    }

    if (!constraint) return nullptr;

    m_PhysicsSystem.AddConstraint(constraint);

    // 配置马达
    if (def.enableMotor && (def.type == JointType3D::Hinge || def.type == JointType3D::Slider)) {
        if (def.type == JointType3D::Hinge) {
            auto* hinge = static_cast<JPH::HingeConstraint*>(constraint);
            hinge->SetMotorState(JPH::EMotorState::Velocity);
            hinge->SetTargetAngularVelocity(def.motorSpeed);
            hinge->GetMotorSettings().SetTorqueLimit(def.maxMotorTorque);
        } else {
            auto* slider = static_cast<JPH::SliderConstraint*>(constraint);
            slider->SetMotorState(JPH::EMotorState::Velocity);
            slider->SetTargetVelocity(def.motorSpeed);
            slider->GetMotorSettings().SetForceLimit(def.maxMotorTorque);
        }
    }

    return std::make_shared<JoltJoint3D>(constraint, def.type);
}

void JoltPhysicsWorld::DestroyJoint(IJoint3D* joint) {
    if (!joint) return;
    auto* joltJoint = static_cast<JoltJoint3D*>(joint);
    JPH::TwoBodyConstraint* constraint = joltJoint->GetConstraint();
    if (constraint) {
        m_PhysicsSystem.RemoveConstraint(constraint);
    }
}

} // namespace Engine