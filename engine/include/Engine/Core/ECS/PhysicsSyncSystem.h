#pragma once

/**
 * @file PhysicsSyncSystem.h
 * @brief 物理同步系统 — ECS ↔ 物理引擎三级同步管线
 *
 * 每帧执行顺序（v3.1 修正版）：
 *
 *   1. PrePhysicsSync (ECS → Physics)
 *      - 检查 RigidBody3DComponent 配置变更
 *      - 检查 TransformComponent 的 isDirty（脚本/编辑器手动修改）
 *      - 通过 BodyInterface 同步到物理引擎
 *
 *   2. PhysicsStep (固定步长)
 *      - FixedTimestepAccumulator 分割真实 dt
 *      - 每次步进前备份 prevPosition/prevRotation
 *      - 调用 IPhysicsWorld3D::Step()
 *
 *   3. PostPhysicsSync (Physics → ECS)
 *      - 将 Jolt 积分后的"真实"位置写入 TransformComponent
 *      - 仅同步活跃（非休眠）物体，提升性能
 *
 *   ★ 渲染插值在渲染系统局部计算，不写回任何 ECS 组件 ★
 */

#include "Engine/Core/ECS/ECS.fwd.h"
#include "Engine/Core/Physics/FixedTimestepAccumulator.h"
#include "Engine/Core/Physics/IJoint3D.h"
#include "Engine/Core/Physics/ICharacterController3D.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/GameObject/GameObject.h"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <functional>

namespace Engine {

class IPhysicsWorld3D;

class PhysicsSyncSystem {
public:
    PhysicsSyncSystem() = default;
    ~PhysicsSyncSystem() = default;

    PhysicsSyncSystem(const PhysicsSyncSystem&) = delete;
    PhysicsSyncSystem& operator=(const PhysicsSyncSystem&) = delete;

    // ── 初始化 ──
    void Init(EntityManager* em, IPhysicsWorld3D* physicsWorld);

    // ── 每帧更新（三级同步管线） ──
    /**
     * @brief 执行完整物理同步管线
     * @param realDt 实际帧时间（秒）
     *
     * v6.0: 新增 ProcessCollisionEvents 路由到 CollisionListenerComponent
     */
    void Update(float32 realDt);

    /**
     * @brief 获取渲染插值因子（供渲染系统使用）
     *
     * 渲染系统应该使用此值进行局部插值，而非修改 TransformComponent。
     */
    float32 GetRenderAlpha() const { return m_Accumulator.GetAlpha(); }

    /** 获取固定步长累加器引用（调试用） */
    const FixedTimestepAccumulator& GetAccumulator() const { return m_Accumulator; }

private:
    /**
     * @brief 步骤 1: ECS → Physics
     * 同步编辑器和脚本对 Transform/RigidBody 的修改到物理引擎
     * v6.0: 新增 Joint3DComponent 扫描创建/销毁约束
     */
    void SyncECSToPhysics();

    /**
     * @brief 步骤 2a: Pre-Step 状态备份
     */
    void BackupState();

    /**
     * @brief 步骤 2b: 执行物理步进
     */
    void StepPhysics(float32 fixedDt);

    /**
     * @brief 步骤 3: Physics → ECS
     */
    void SyncPhysicsToECS();

    /**
     * @brief v6.0: 消费碰撞事件队列并路由到 CollisionListenerComponent
     * 在 StepPhysics 之后 SyncPhysicsToECS 之前调用
     */
    void ProcessCollisionEvents();

    EntityManager*    m_EntityManager = nullptr;
    IPhysicsWorld3D*  m_PhysicsWorld  = nullptr;
    FixedTimestepAccumulator m_Accumulator{1.0f / 60.0f};

    // v6.0: 已创建的关节映射 (EntityHandle.Index() → IJoint3D)
    std::unordered_map<uint64, std::shared_ptr<IJoint3D>> m_Joints;
};

} // namespace Engine