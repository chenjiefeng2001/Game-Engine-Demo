#pragma once

/**
 * @file JoltPhysicsWorld.h
 * @brief Jolt Physics 3D 物理世界实现
 *
 * 实现 IPhysicsWorld3D 接口，替换 NullPhysicsWorld3D。
 * 集成 Engine::JobSystem 用于多线程模拟。
 *
 * v4.0 变更：
 *   - 添加 LockFreeEventQueue m_EventQueue，Worker 线程只传 BodyID
 *   - 添加 ProcessCollisionEvents() 在主线程安全消费碰撞事件
 *   - 添加 BatchSetKinematicTargets / BatchGetTransforms 批量 API
 *   - 添加 GetPhysicsSystem() 供 PhysicsSyncSystem 访问 BodyLockInterface
 */

#include "Engine/Core/Physics/IPhysicsWorld3D.h"
#include "Engine/Core/Physics/IPhysicsBody3D.h"
#include "Engine/Core/Physics/PhysicsLayers.h"
#include "Engine/Core/Physics/LockFreeEventQueue.h"
#include "Engine/Jolt/JoltJobSystemAdapter.h"

// Jolt.h 必须先于所有其他 Jolt 头文件包含，确保宏正确定义
#include <Jolt/Jolt.h>

// 引擎 Jolt 包装器（必须在 Jolt.h 之后）
#include "Engine/Jolt/JoltDebugRenderer.h"

#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterMask.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Core/TempAllocator.h>

#include <memory>
#include <unordered_map>

namespace Engine {

class JoltPhysicsWorld final : public IPhysicsWorld3D {
public:
    JoltPhysicsWorld();
    ~JoltPhysicsWorld() override;

    // ── IPhysicsWorld3D 接口 ──

    bool Init(const PhysicsWorldConfig3D& config) override;
    void Shutdown() override;

    void Step(float32 dt, int32 collisionSteps = 1) override;

    std::shared_ptr<IPhysicsBody3D> CreateBody(const BodyDef3D& def) override;
    void DestroyBody(IPhysicsBody3D* body) override;

    std::shared_ptr<IJoint3D> CreateJoint(const JointDef3D& def) override;
    void DestroyJoint(IJoint3D* joint) override;

    // ── 刚体生命周期 ──
    IPhysicsBody3D* GetBodyByID(uint64 bodyID) override;
    void RemoveBody(uint64 bodyID) override;
    bool IsBodyValid(uint64 bodyID) override;

    // ── 查询 ──
    std::vector<RayCastResult3D> RayCast(const Vec3& from, const Vec3& to) override;
    std::vector<IPhysicsBody3D*> QueryAABB(const Vec3& center, const Vec3& halfSize) override;
    std::vector<IPhysicsBody3D*> QuerySphere(const Vec3& center, float32 radius) override;

    void SetGravity(const Vec3& gravity) override;
    Vec3 GetGravity() const override;

    void SetContactBeginCallback(ContactCallback3D callback) override;
    void SetContactEndCallback(ContactCallback3D callback) override;
    void SetContactPreSolveCallback(ContactFilterCallback3D callback) override;
    void SetContactPersistCallback(ContactPersistCallback3D callback) override;

    void SetDebugDraw(IPhysicsDebugDraw3D* draw) override;
    void DebugDraw() override;

    Stats GetStats() const override;
    void* GetNativeWorld() override { return &m_PhysicsSystem; }

    // ── v4.0: 碰撞事件消费（主线程调用，在 Step 之后同步阶段之前） ──
    void ProcessCollisionEvents();

    // ── v4.0: Batch API（工业级批量变换操作） ──
    void BatchSetKinematicTargets(
        uint32 count, const uint64* bodyIDs,
        const Vec3* positions, const Quat* rotations) override;

    void BatchGetTransforms(
        uint32 count, const uint64* bodyIDs,
        Vec3* outPositions, Quat* outRotations) override;

    // ── 工具函数 ──
    /** 将 Engine::BodyDef3D 转换为 Jolt 的 BodyCreationSettings */
    JPH::BodyCreationSettings ToJoltBodySettings(const BodyDef3D& def);

    /** 将 JPH::BodyID 包装为 uint64 用于 ECS */
    static uint64 BodyIDToU64(JPH::BodyID id) {
        return static_cast<uint64>(id.GetIndexAndSequenceNumber());
    }

    /** 将 uint64 还原为 JPH::BodyID */
    static JPH::BodyID U64ToBodyID(uint64 id) {
        return JPH::BodyID(static_cast<uint32>(id));
    }

    /** 获取 Jolt PhysicsSystem 引用（供 PhysicsSyncSystem 用于 BodyLock） */
    JPH::PhysicsSystem& GetPhysicsSystem() { return m_PhysicsSystem; }

    /** 获取碰撞事件队列引用（供 PhysicsSyncSystem 用于 RouteCollisionEvents） */
    LockFreeEventQueue& GetEventQueue() { return m_EventQueue; }

private:
    // ── Jolt 内部对象 ──
    JPH::TempAllocatorImpl*        m_TempAllocator   = nullptr;
    JoltJobSystemAdapter*          m_JobSystemAdapter = nullptr;
    JPH::PhysicsSystem             m_PhysicsSystem;

    // BroadPhase / ObjectLayer 映射
    JPH::BroadPhaseLayerInterfaceTable* m_BPInterface = nullptr;
    JPH::ObjectVsBroadPhaseLayerFilter* m_ObjectVsBPFilter = nullptr;
    JPH::ObjectLayerPairFilterTable* m_ObjectLayerFilter = nullptr;

    // ── v4.0: 无锁碰撞事件队列（Worker 线程只 Push BodyID） ──
    LockFreeEventQueue m_EventQueue{256};

    // 碰撞事件回调
    ContactCallback3D        m_ContactBegin;
    ContactCallback3D        m_ContactEnd;
    ContactFilterCallback3D  m_ContactPreSolve;
    ContactPersistCallback3D m_ContactPersist;

    // 碰撞事件监听器（Jolt 原生）
    class ContactListenerImpl;
    ContactListenerImpl* m_ContactListener = nullptr;

    // 调试绘制
    IPhysicsDebugDraw3D* m_DebugDraw = nullptr;
    std::unique_ptr<JoltDebugRenderer> m_DebugRenderer;

    // ECS BodyID → IPhysicsBody3D 映射
    std::unordered_map<uint64, std::shared_ptr<IPhysicsBody3D>> m_BodyMap;
};

} // namespace Engine