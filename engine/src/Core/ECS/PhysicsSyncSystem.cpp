/**
 * @file PhysicsSyncSystem.cpp
 * @brief 物理同步系统实现 — ECS ↔ Jolt 三级同步管线（已激活）
 */

#include "Engine/Core/ECS/PhysicsSyncSystem.h"
#include "Engine/Core/ECS/EntityManager.h"
#include "Engine/Core/ECS/PhysicsComponents.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/Physics/IPhysicsWorld3D.h"
#include "Engine/Core/Physics/IPhysicsBody3D.h"
#include "Engine/Core/Log.h"

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
    SyncECSToPhysics();
    int steps = m_Accumulator.Advance(realDt);
    for (int i = 0; i < steps; ++i) {
        BackupState();
        StepPhysics(m_Accumulator.GetFixedDt());
    }
    if (steps > 0) SyncPhysicsToECS();
}

void PhysicsSyncSystem::SyncECSToPhysics() {
    auto query = m_EntityManager->Query()
        .With<RigidBody3DComponent, PhysicsRuntimeComponent>()
        .Build();
    for (auto [chunk, start, count] : query) {
        auto runtimes = chunk->GetComponentSpan<PhysicsRuntimeComponent>();
        for (uint32 i = start; i < start + count; ++i) {
            auto& rt = runtimes[i];
            if (!rt.isDirty || rt.runtimeBodyID == 0) continue;
            auto* body = m_PhysicsWorld->GetBodyByID(rt.runtimeBodyID);
            if (body) { rt.isDirty = false; }
        }
    }
}

void PhysicsSyncSystem::BackupState() {
    auto query = m_EntityManager->Query()
        .With<TransformComponent, PhysicsRuntimeComponent>()
        .Build();
    for (auto [chunk, start, count] : query) {
        auto transforms = chunk->GetComponentSpan<TransformComponent>();
        auto runtimes   = chunk->GetComponentSpan<PhysicsRuntimeComponent>();
        for (uint32 i = start; i < start + count; ++i) {
            runtimes[i].prevPosition = transforms[i].GetPosition();
        }
    }
}

void PhysicsSyncSystem::StepPhysics(float32 fixedDt) {
    if (m_PhysicsWorld) m_PhysicsWorld->Step(fixedDt);
}

void PhysicsSyncSystem::SyncPhysicsToECS() {
    auto query = m_EntityManager->Query()
        .With<TransformComponent, PhysicsRuntimeComponent>()
        .Build();
    for (auto [chunk, start, count] : query) {
        auto transforms = chunk->GetComponentSpan<TransformComponent>();
        auto runtimes   = chunk->GetComponentSpan<PhysicsRuntimeComponent>();
        for (uint32 i = start; i < start + count; ++i) {
            if (!runtimes[i].isActive || runtimes[i].runtimeBodyID == 0) continue;
            auto* body = m_PhysicsWorld->GetBodyByID(runtimes[i].runtimeBodyID);
            if (body) {
                transforms[i].SetPosition(body->GetPosition());
                transforms[i].SetRotation(body->GetRotation());
            }
        }
    }
}

} // namespace Engine