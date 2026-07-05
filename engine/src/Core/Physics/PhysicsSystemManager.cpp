/**
 * @file PhysicsSystemManager.cpp
 * @brief 物理系统管理器实现 — Jolt Physics + Box2D + 固定步长
 */

#include "Engine/Core/Physics/PhysicsSystemManager.h"
#include "Engine/Jolt/JoltPhysicsWorld.h"
#include "Engine/Core/Log.h"

namespace Engine {

    // ════════════════════════════════════════════
    // 2D 物理
    // ════════════════════════════════════════════

    std::shared_ptr<IPhysicsWorld> PhysicsSystemManager::CreateWorld2D(const Vec2& gravity) {
        Log::Warn("PhysicsSystemManager::CreateWorld2D - "
                  "Use the existing Box2D-based initialization path.");
        return nullptr;
    }

    void PhysicsSystemManager::SetWorld2D(std::shared_ptr<IPhysicsWorld> world) {
        m_World2D = std::move(world);
    }

    const char* PhysicsSystemManager::GetEngineName2D() const {
        return "Box2D 2.4.x";
    }

    // ════════════════════════════════════════════
    // 3D 物理 — Jolt Physics
    // ════════════════════════════════════════════

    std::shared_ptr<IPhysicsWorld3D> PhysicsSystemManager::CreateWorld3D(
        const PhysicsWorldConfig3D& config)
    {
        auto world = std::make_shared<JoltPhysicsWorld>();
        if (!world->Init(config)) {
            Log::Error("[Physics3D] Failed to initialize JoltPhysicsWorld");
            return nullptr;
        }
        m_World3D = world;
        Log::Info("[Physics3D] Created JoltPhysicsWorld (maxBodies={})", config.maxBodies);
        return world;
    }

    void PhysicsSystemManager::SetWorld3D(std::shared_ptr<IPhysicsWorld3D> world) {
        m_World3D = std::move(world);
    }

    const char* PhysicsSystemManager::GetEngineName3D() const {
        return "Jolt Physics 5.5";
    }

    // ════════════════════════════════════════════
    // 统一步进（固定时间步长）
    // ════════════════════════════════════════════

    void PhysicsSystemManager::StepAll(float32 dt) {
        // 固定时间步长累加（防止螺旋式死亡，上限 8 步）
        int32 steps = m_FixedAccumulator.Advance(dt);
        if (steps <= 0) return;

        for (int32 i = 0; i < steps; ++i) {
            float32 fixedDt = m_FixedAccumulator.GetFixedDt();

            // 2D 步进
            if (m_World2D) {
                m_World2D->Step(fixedDt);
            }

            // 3D 步进
            if (m_World3D) {
                m_World3D->Step(fixedDt, 1);
            }
        }

        // 渲染插值因子（供 Renderer 读取，不在物理管线的关键路径上）
        m_RenderAlpha = m_FixedAccumulator.GetAlpha();
    }

} // namespace Engine
