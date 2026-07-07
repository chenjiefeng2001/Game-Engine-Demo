/**
 * @file PhysicsSyncSystem.cpp
 * @brief 物理同步系统实现 — ECS ↔ Jolt 三级同步管线（已激活）
 *
 * v4.0 变更：
 *   - SyncECSToPhysics 新增 Collider 拓扑同步（Box/Sphere/Capsule isDirty → SetShape）
 *   - SyncPhysicsToECS 使用四元数直通（GetRotationQuat）避免万向锁
 *   - 新增 ProcessCollisionEvents 调用（在主线程安全消费 Jolt 碰撞事件）
 */

#include "Engine/Core/ECS/PhysicsSyncSystem.h"
#include "Engine/Core/ECS/EntityManager.h"
#include "Engine/Core/ECS/PhysicsComponents.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/Physics/IPhysicsWorld3D.h"
#include "Engine/Core/Physics/IPhysicsBody3D.h"
#include "Engine/Jolt/JoltPhysicsWorld.h"
#include "Engine/Core/Log.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/Body.h>

namespace Engine {

void PhysicsSyncSystem::Init(EntityManager* em, IPhysicsWorld3D* physicsWorld) {
    m_EntityManager = em;
    m_PhysicsWorld  = physicsWorld;

    // 注册组件移除回调 — ECS 销毁实体时同步清理 Jolt Body
    if (m_EntityManager) {
        m_EntityManager->SetComponentRemovedCallback(
            [this](EntityHandle entity, ComponentTypeID typeID) {
                if (typeID == ComponentType<PhysicsRuntimeComponent>::ID()) {
                    auto* rt = m_EntityManager->GetComponent<PhysicsRuntimeComponent>(entity);
                    if (rt && rt->runtimeBodyID != 0 && m_PhysicsWorld) {
                        m_PhysicsWorld->RemoveBody(rt->runtimeBodyID);
                    }
                }
            }
        );
    }
}

void PhysicsSyncSystem::Update(float32 realDt) {
    if (!m_EntityManager || !m_PhysicsWorld) return;

    // 步骤 1: ECS → Physics（同步配置变更 + Collider 拓扑）
    SyncECSToPhysics();

    // 步骤 2: 固定步长物理步进
    int steps = m_Accumulator.Advance(realDt);
    for (int i = 0; i < steps; ++i) {
        BackupState();
        StepPhysics(m_Accumulator.GetFixedDt());

        // v4.0: 每次物理步进后消费碰撞事件（在主线程安全解析）
        if (m_PhysicsWorld) {
            auto* joltWorld = static_cast<JoltPhysicsWorld*>(m_PhysicsWorld);
            joltWorld->ProcessCollisionEvents();
        }
    }

    // 步骤 3: Physics → ECS（同步变换）
    if (steps > 0) SyncPhysicsToECS();
}

void PhysicsSyncSystem::SyncECSToPhysics() {
    if (!m_EntityManager || !m_PhysicsWorld) return;

    // ── 3.1: Transform/RigidBody 配置同步 ──
    auto configQuery = m_EntityManager->Query()
        .With<RigidBody3DComponent>()
        .With<PhysicsRuntimeComponent>()
        .Build();
    for (auto& range : configQuery) {
        auto runtimes = range.chunk->GetComponentSpan<PhysicsRuntimeComponent>();
        for (uint32 i = range.startRow; i < range.startRow + range.count; ++i) {
            auto& rt = runtimes[i];
            if (!rt.isDirty || rt.runtimeBodyID == 0) continue;
            auto* body = m_PhysicsWorld->GetBodyByID(rt.runtimeBodyID);
            if (body) { rt.isDirty = false; }
        }
    }

    // ── 3.2: Collider 拓扑同步（v4.0: 使用 Jolt BodyLock 直接替换 Shape） ──
    auto* joltWorld = static_cast<JoltPhysicsWorld*>(m_PhysicsWorld);
    JPH::PhysicsSystem& physicsSys = joltWorld->GetPhysicsSystem();

    // Box Collider
    auto boxQuery = m_EntityManager->Query()
        .With<RigidBody3DComponent>()
        .With<PhysicsRuntimeComponent>()
        .With<BoxCollider3DComponent>()
        .Build();
    for (auto& range : boxQuery) {
        auto runtimes = range.chunk->GetComponentSpan<PhysicsRuntimeComponent>();
        auto boxes    = range.chunk->GetComponentSpan<BoxCollider3DComponent>();
        for (uint32 i = range.startRow; i < range.startRow + range.count; ++i) {
            if (!boxes[i].isDirty || runtimes[i].runtimeBodyID == 0) continue;
            JPH::BodyID bodyId = JoltPhysicsWorld::U64ToBodyID(runtimes[i].runtimeBodyID);
            JPH::BodyLockWrite lock(physicsSys.GetBodyLockInterface(), bodyId);
            if (lock.Succeeded()) {
                JPH::Body& body = lock.GetBody();
                JPH::BoxShapeSettings boxSettings(JPH::Vec3(
                    boxes[i].halfExtents.x, boxes[i].halfExtents.y, boxes[i].halfExtents.z));
                JPH::ShapeRefC newShape = boxSettings.Create().Get();
                body.SetShape(newShape, true, JPH::EActivation::Activate);
            }
            boxes[i].isDirty = false;
        }
    }

    // Sphere Collider
    auto sphereQuery = m_EntityManager->Query()
        .With<RigidBody3DComponent>()
        .With<PhysicsRuntimeComponent>()
        .With<SphereCollider3DComponent>()
        .Build();
    for (auto& range : sphereQuery) {
        auto runtimes = range.chunk->GetComponentSpan<PhysicsRuntimeComponent>();
        auto spheres  = range.chunk->GetComponentSpan<SphereCollider3DComponent>();
        for (uint32 i = range.startRow; i < range.startRow + range.count; ++i) {
            if (!spheres[i].isDirty || runtimes[i].runtimeBodyID == 0) continue;
            JPH::BodyID bodyId = JoltPhysicsWorld::U64ToBodyID(runtimes[i].runtimeBodyID);
            JPH::BodyLockWrite lock(physicsSys.GetBodyLockInterface(), bodyId);
            if (lock.Succeeded()) {
                JPH::Body& body = lock.GetBody();
                JPH::SphereShapeSettings sphereSettings(spheres[i].radius);
                JPH::ShapeRefC newShape = sphereSettings.Create().Get();
                body.SetShape(newShape, true, JPH::EActivation::Activate);
            }
            spheres[i].isDirty = false;
        }
    }

    // Capsule Collider
    auto capsuleQuery = m_EntityManager->Query()
        .With<RigidBody3DComponent>()
        .With<PhysicsRuntimeComponent>()
        .With<CapsuleCollider3DComponent>()
        .Build();
    for (auto& range : capsuleQuery) {
        auto runtimes  = range.chunk->GetComponentSpan<PhysicsRuntimeComponent>();
        auto capsules  = range.chunk->GetComponentSpan<CapsuleCollider3DComponent>();
        for (uint32 i = range.startRow; i < range.startRow + range.count; ++i) {
            if (!capsules[i].isDirty || runtimes[i].runtimeBodyID == 0) continue;
            JPH::BodyID bodyId = JoltPhysicsWorld::U64ToBodyID(runtimes[i].runtimeBodyID);
            JPH::BodyLockWrite lock(physicsSys.GetBodyLockInterface(), bodyId);
            if (lock.Succeeded()) {
                JPH::Body& body = lock.GetBody();
                JPH::CapsuleShapeSettings capsuleSettings(
                    capsules[i].height * 0.5f, capsules[i].radius);
                JPH::ShapeRefC newShape = capsuleSettings.Create().Get();
                body.SetShape(newShape, true, JPH::EActivation::Activate);
            }
            capsules[i].isDirty = false;
        }
    }
}

void PhysicsSyncSystem::BackupState() {
    auto query = m_EntityManager->Query()
        .With<TransformComponent>()
        .With<PhysicsRuntimeComponent>()
        .Build();
    for (auto& range : query) {
        auto transforms = range.chunk->GetComponentSpan<TransformComponent>();
        auto runtimes   = range.chunk->GetComponentSpan<PhysicsRuntimeComponent>();
        for (uint32 i = range.startRow; i < range.startRow + range.count; ++i) {
            runtimes[i].prevPosition = transforms[i].GetPosition();
            runtimes[i].prevRotation = transforms[i].GetRotationQuat();
        }
    }
}

void PhysicsSyncSystem::StepPhysics(float32 fixedDt) {
    if (m_PhysicsWorld) m_PhysicsWorld->Step(fixedDt);
}

void PhysicsSyncSystem::SyncPhysicsToECS() {
    auto query = m_EntityManager->Query()
        .With<TransformComponent>()
        .With<PhysicsRuntimeComponent>()
        .Build();
    for (auto& range : query) {
        auto transforms = range.chunk->GetComponentSpan<TransformComponent>();
        auto runtimes   = range.chunk->GetComponentSpan<PhysicsRuntimeComponent>();
        for (uint32 i = range.startRow; i < range.startRow + range.count; ++i) {
            if (!runtimes[i].isActive || runtimes[i].runtimeBodyID == 0) continue;
            auto* body = m_PhysicsWorld->GetBodyByID(runtimes[i].runtimeBodyID);
            if (body) {
                transforms[i].SetPosition(body->GetPosition());
                // v4.0: 使用四元数直通，避免欧拉角万向锁
                transforms[i].SetRotationQuat(body->GetRotationQuat());
            }
        }
    }
}

} // namespace Engine