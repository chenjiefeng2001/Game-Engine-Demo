/**
 * @file PhysicsSyncSystem.cpp
 * @brief 物理同步系统实现 — ECS ↔ Jolt 三级同步管线
 *
 * v7.0 修复：
 *   - 关节生命周期：延迟创建（等待两个 BodyID 都就绪）+ 级联销毁
 *   - CharacterController 固定步进集成
 *   - CollisionListener 事件路由
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

    if (m_EntityManager) {
        m_EntityManager->SetComponentRemovedCallback(
            [this](EntityHandle entity, ComponentTypeID typeID) {
                uint64 idx = entity.Index();

                // v7.0: 级联销毁关节 — 如果实体被销毁，清理所有以它为关节目标的 Joint
                if (typeID == ComponentType<Joint3DComponent>::ID()) {
                    auto jt = m_Joints.find(idx);
                    if (jt != m_Joints.end()) {
                        if (m_PhysicsWorld) m_PhysicsWorld->DestroyJoint(jt->second.get());
                        m_Joints.erase(jt);
                    }
                }

                // v7.0: 级联销毁 — 查找所有指向 this 的 Joint3DComponent
                if (typeID == ComponentType<PhysicsRuntimeComponent>::ID()) {
                    // 先清理关节
                    auto jt = m_Joints.find(idx);
                    if (jt != m_Joints.end()) {
                        if (m_PhysicsWorld) m_PhysicsWorld->DestroyJoint(jt->second.get());
                        m_Joints.erase(jt);
                    }

                    // 清理所有以该实体为目标的 Joint
                    std::vector<uint64> toRemove;
                    for (auto& [jointIdx, joint] : m_Joints) {
                        auto* comp = m_EntityManager->GetComponent<Joint3DComponent>(
                            EntityHandle::FromIndex(jointIdx));
                        if (comp && (comp->entityA.Index() == idx || comp->entityB.Index() == idx)) {
                            if (m_PhysicsWorld) m_PhysicsWorld->DestroyJoint(joint.get());
                            toRemove.push_back(jointIdx);
                        }
                    }
                    for (uint64 rid : toRemove) m_Joints.erase(rid);

                    // 再清理 Body
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

    // 步骤 1: ECS → Physics
    SyncECSToPhysics();

    // 步骤 2: 固定步长物理步进
    int steps = m_Accumulator.Advance(realDt);
    for (int i = 0; i < steps; ++i) {
        float32 fixedDt = m_Accumulator.GetFixedDt();
        BackupState();
        StepPhysics(fixedDt);

        // v7.0: CharacterController 固定步进 — 在固定步长中执行，避免可变帧率卡顿
        for (auto& cct : m_CharacterControllers) {
            if (cct.second) cct.second->Update(fixedDt);
        }

        // 消费碰撞事件
        if (m_PhysicsWorld) {
            auto* joltWorld = static_cast<JoltPhysicsWorld*>(m_PhysicsWorld);
            joltWorld->ProcessCollisionEvents();
        }

        // v7.0: 碰撞事件路由到 CollisionListenerComponent
        RouteCollisionEvents();
    }

    // 步骤 3: Physics → ECS
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

    // ── 3.2: Collider 拓扑同步 ──
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
                JPH::BoxShapeSettings boxSettings(
                    JPH::Vec3(boxes[i].halfExtents.x, boxes[i].halfExtents.y, boxes[i].halfExtents.z));
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
                JPH::CapsuleShapeSettings capsuleSettings(capsules[i].height * 0.5f, capsules[i].radius);
                JPH::ShapeRefC newShape = capsuleSettings.Create().Get();
                body.SetShape(newShape, true, JPH::EActivation::Activate);
            }
            capsules[i].isDirty = false;
        }
    }

    // ── 3.3: Joint 延迟创建（v7.0: 等待两个body都就绪）──
    auto jointQuery = m_EntityManager->Query()
        .With<Joint3DComponent>()
        .Build();
    for (auto& range : jointQuery) {
        auto joints = range.chunk->GetComponentSpan<Joint3DComponent>();
        for (uint32 i = range.startRow; i < range.startRow + range.count; ++i) {
            uint64 idx = m_EntityManager->GetEntityByChunkRow(range.chunk, range.startRow + i).Index();
            if (m_Joints.count(idx) > 0) continue; // 已创建

            auto& joint = joints[i];

            // 延迟创建：检查两个实体的 BodyID 都有效
            auto* rtA = m_EntityManager->GetComponent<PhysicsRuntimeComponent>(joint.entityA);
            auto* rtB = m_EntityManager->GetComponent<PhysicsRuntimeComponent>(joint.entityB);
            if (!rtA || !rtB || rtA->runtimeBodyID == 0 || rtB->runtimeBodyID == 0) continue;

            auto* bodyA = m_PhysicsWorld->GetBodyByID(rtA->runtimeBodyID);
            auto* bodyB = m_PhysicsWorld->GetBodyByID(rtB->runtimeBodyID);
            if (!bodyA || !bodyB) continue;

            JointDef3D jdef;
            jdef.type = joint.jointType;
            jdef.bodyA = bodyA;
            jdef.bodyB = bodyB;
            jdef.anchorPointA = joint.anchorPointA;
            jdef.anchorPointB = joint.anchorPointB;
            jdef.hingeAxis = joint.hingeAxis;
            jdef.sliderAxis = joint.sliderAxis;
            jdef.distance = joint.distance;
            jdef.limits = joint.limits;
            jdef.enableMotor = joint.enableMotor;
            jdef.motorSpeed = joint.motorSpeed;
            jdef.maxMotorTorque = joint.maxMotorTorque;

            auto newJoint = m_PhysicsWorld->CreateJoint(jdef);
            if (newJoint) {
                m_Joints[idx] = newJoint;
            }
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
                transforms[i].SetRotationQuat(body->GetRotationQuat());
            }
        }
    }

    // v7.0: CCT → Transform 同步
    for (auto& [_, cct] : m_CharacterControllers) {
        if (cct) {
            auto* owner = static_cast<GameObject*>(cct->GetUserData());
            if (owner) {
                owner->GetTransform().SetPosition(cct->GetPosition());
            }
        }
    }
}

// v7.0: 碰撞事件路由到 CollisionListenerComponent
void PhysicsSyncSystem::RouteCollisionEvents() {
    if (!m_PhysicsWorld) return;
    auto* joltWorld = static_cast<JoltPhysicsWorld*>(m_PhysicsWorld);

    CollisionEvent evt;
    LockFreeEventQueue& queue = joltWorld->GetEventQueue();
    while (queue.Pop(evt)) {
        // 查找拥有该 BodyID 的实体
        m_EntityManager->Query().With<CollisionListenerComponent>().With<PhysicsRuntimeComponent>().Build();
        // 简化：通过 bBodyMap 反查 Entity
        for (auto& range : m_EntityManager->Query()
            .With<CollisionListenerComponent>()
            .With<PhysicsRuntimeComponent>()
            .Build()) {
            auto listeners = range.chunk->GetComponentSpan<CollisionListenerComponent>();
            auto runtimes  = range.chunk->GetComponentSpan<PhysicsRuntimeComponent>();
            for (uint32 i = range.startRow; i < range.startRow + range.count; ++i) {
                if (runtimes[i].runtimeBodyID == evt.bodyIDA) {
                    if (evt.type == CollisionEvent::Begin && listeners[i].onCollisionEnter) {
                        listeners[i].onCollisionEnter(evt.bodyIDB, Vec3(0,0,0));
                    }
                    if (evt.type == CollisionEvent::End && listeners[i].onCollisionExit) {
                        listeners[i].onCollisionExit(evt.bodyIDB);
                    }
                }
                if (runtimes[i].runtimeBodyID == evt.bodyIDB) {
                    if (evt.type == CollisionEvent::Begin && listeners[i].onCollisionEnter) {
                        listeners[i].onCollisionEnter(evt.bodyIDA, Vec3(0,0,0));
                    }
                    if (evt.type == CollisionEvent::End && listeners[i].onCollisionExit) {
                        listeners[i].onCollisionExit(evt.bodyIDA);
                    }
                }
            }
        }
    }
}

// v7.0: 注册 CCT 到同步系统
void PhysicsSyncSystem::RegisterCharacterController(uint64 entityId, ICharacterController3D* cct) {
    m_CharacterControllers[entityId] = cct;
}

void PhysicsSyncSystem::UnregisterCharacterController(uint64 entityId) {
    m_CharacterControllers.erase(entityId);
}

} // namespace Engine