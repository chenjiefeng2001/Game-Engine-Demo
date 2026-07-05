/**
 * @file PhysicsSyncSystem.cpp
 * @brief 物理同步系统实现 — 三级同步管线
 *
 * == v3.1 设计要点 ==
 *
 * 1. TransformComponent 只存"真实"位置（物理积分后的结果）
 * 2. 渲染插值在渲染系统局部计算（`Lerp(prev, current, alpha)`），不写回
 * 3. 休眠物体跳过同步（通过 IsActive 检查）
 * 4. PreSync 只同步 isDirty 的物体（脚本/编辑器强制修改）
 */

#include "Engine/Core/ECS/PhysicsSyncSystem.h"
#include "Engine/Core/ECS/EntityManager.h"
#include "Engine/Core/ECS/PhysicsComponents.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/Physics/IPhysicsWorld3D.h"
#include "Engine/Core/Physics/IPhysicsBody3D.h"
#include "Engine/Core/Log.h"
#include <cassert>

namespace Engine {

// ═══════════════════════════════════════════════════════════
// 初始化
// ═══════════════════════════════════════════════════════════

void PhysicsSyncSystem::Init(EntityManager* em, IPhysicsWorld3D* physicsWorld) {
    m_EntityManager = em;
    m_PhysicsWorld  = physicsWorld;

    assert(m_EntityManager != nullptr && "EntityManager must not be null");
    assert(m_PhysicsWorld  != nullptr && "PhysicsWorld must not be null");

    Log::Info("[PhysicsSync] Initialized");
}

// ═══════════════════════════════════════════════════════════
// 每帧更新 — 三级同步管线
// ═══════════════════════════════════════════════════════════

void PhysicsSyncSystem::Update(float32 realDt) {
    if (!m_EntityManager || !m_PhysicsWorld) return;

    // ── 步骤 1: ECS → Physics ──
    // 同步脚本/编辑器对 Transform/RigidBody 的修改
    SyncECSToPhysics();

    // ── 步骤 2: 固定步长物理步进 ──
    int32 steps = m_Accumulator.Advance(realDt);
    for (int32 i = 0; i < steps; ++i) {
        // 2a: 备份当前状态（用于渲染插值）
        BackupState();
        // 2b: 物理步进
        StepPhysics(m_Accumulator.GetFixedDt());
    }

    if (steps > 0) {
        // ── 步骤 3: Physics → ECS ──
        // 将 Jolt 积分后的位置写回 TransformComponent
        SyncPhysicsToECS();
    }
}

// ═══════════════════════════════════════════════════════════
// 步骤 1: ECS → Physics
// ═══════════════════════════════════════════════════════════

void PhysicsSyncSystem::SyncECSToPhysics() {
    // 查询所有既有 RigidBody3D 又有 PhysicsRuntime 的实体
    auto query = m_EntityManager->Query()
        .With<RigidBody3DComponent, PhysicsRuntimeComponent>()
        .Build();

    for (auto [chunk, start, count] : query) {
        auto rigidbodies = chunk->GetComponentSpan<RigidBody3DComponent>();
        auto runtimes    = chunk->GetComponentSpan<PhysicsRuntimeComponent>();

        for (uint32 i = start; i < start + count; ++i) {
            auto& rb = rigidbodies[i];
            auto& rt = runtimes[i];

            // 只有标记了 isDirty 才需要同步（减少不必要的 BodyInterface 调用）
            if (!rt.isDirty) continue;

            // 获取物理引擎 Body 并同步
            // 注意：这里通过 IPhysicsBody3D 接口操作，具体实现由 Jolt 完成
            // 实际项目中，通过 PhysicsSystemManager 持有 Body 映射
            // 简化：这里用 runtimeBodyID 查找并同步
            if (rt.runtimeBodyID != 0) {
                // 实际调用：
                // IPhysicsBody3D* body = m_PhysicsWorld->GetBodyByID(rt.runtimeBodyID);
                // body->SetPosition(transform.position);
                // body->SetRotation(transform.rotation);
            }

            rt.isDirty = false;
        }
    }
}

// ═══════════════════════════════════════════════════════════
// 步骤 2a: Pre-Step 状态备份
// ═══════════════════════════════════════════════════════════

void PhysicsSyncSystem::BackupState() {
    auto query = m_EntityManager->Query()
        .With<TransformComponent, PhysicsRuntimeComponent>()
        .Build();

    for (auto [chunk, start, count] : query) {
        auto transforms = chunk->GetComponentSpan<TransformComponent>();
        auto runtimes   = chunk->GetComponentSpan<PhysicsRuntimeComponent>();

        for (uint32 i = start; i < start + count; ++i) {
            auto& t  = transforms[i];
            auto& rt = runtimes[i];

            // 备份当前位置到 prev*（物理步进前）
            rt.prevPosition = t.GetPosition();
            // 从 TransformComponent 获取当前旋转并转为 Quat
            // 简化：直接使用 Vec3 rotation → Quat 的转换
            // 实际项目中通过 glm::quat(glm::radians(euler)) 转换
            // rt.prevRotation = Quat::FromEuler(t.GetRotation());
        }
    }
}

// ═══════════════════════════════════════════════════════════
// 步骤 2b: 物理步进
// ═══════════════════════════════════════════════════════════

void PhysicsSyncSystem::StepPhysics(float32 fixedDt) {
    if (m_PhysicsWorld) {
        m_PhysicsWorld->Step(fixedDt);
    }
}

// ═══════════════════════════════════════════════════════════
// 步骤 3: Physics → ECS
// ═══════════════════════════════════════════════════════════

void PhysicsSyncSystem::SyncPhysicsToECS() {
    // 只同步活跃（非休眠）的物体
    auto query = m_EntityManager->Query()
        .With<TransformComponent, PhysicsRuntimeComponent>()
        .Build();

    for (auto [chunk, start, count] : query) {
        auto transforms = chunk->GetComponentSpan<TransformComponent>();
        auto runtimes   = chunk->GetComponentSpan<PhysicsRuntimeComponent>();

        for (uint32 i = start; i < start + count; ++i) {
            auto& t  = transforms[i];
            auto& rt = runtimes[i];

            // 跳过非活跃（休眠）物体
            if (!rt.isActive) continue;

            // 从物理引擎读取积分后的位置
            if (rt.runtimeBodyID != 0) {
                // 实际调用：
                // IPhysicsBody3D* body = m_PhysicsWorld->GetBodyByID(rt.runtimeBodyID);
                // t.SetPosition(body->GetPosition());      // ★ 写回的是"真实"坐标
                // t.SetRotation(body->GetRotationAsEuler());
            }
        }
    }
}

} // namespace Engine