#pragma once

/**
 * @file JoltPhysicsWorld.h
 * @brief Jolt Physics 3D 物理世界实现
 *
 * 实现 IPhysicsWorld3D 接口，替换 NullPhysicsWorld3D。
 * 集成 Engine::JobSystem 用于多线程模拟。
 */

#include "Engine/Core/Physics/IPhysicsWorld3D.h"
#include "Engine/Core/Physics/IPhysicsBody3D.h"
#include "Engine/Core/Physics/PhysicsLayers.h"
#include "Engine/Core/Physics/LockFreeEventQueue.h"
#include "Engine/Jolt/JoltJobSystemAdapter.h"

#include <Jolt/Jolt.h>
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

private:
    // ── Jolt 内部对象 ──
    JPH::TempAllocatorImpl*        m_TempAllocator   = nullptr;
    JoltJobSystemAdapter*          m_JobSystemAdapter = nullptr;
    JPH::PhysicsSystem             m_PhysicsSystem;

    // BroadPhase / ObjectLayer 映射
    JPH::BroadPhaseLayerInterfaceTable* m_BPInterface = nullptr;
    JPH::ObjectVsBroadPhaseLayerFilter* m_ObjectVsBPFilter = nullptr;
    JPH::ObjectLayerPairFilterTable* m_ObjectLayerFilter = nullptr;

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

    // ECS BodyID → IPhysicsBody3D 映射
    std::unordered_map<uint64, std::shared_ptr<IPhysicsBody3D>> m_BodyMap;
};

} // namespace Engine