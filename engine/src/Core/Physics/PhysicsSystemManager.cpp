/**
 * @file PhysicsSystemManager.cpp
 * @brief 物理系统管理器实现
 *
 * 注意：
 *   当前 3D 物理接口的实现是一个空桩（Stub），仅用于预留接口。
 *   实际的 Jolt/PhysX/Bullet 实现将在后续 PR 中添加。
 */

#include "Engine/Core/Physics/PhysicsSystemManager.h"
#include "Engine/Core/Log.h"

namespace Engine {

    // ════════════════════════════════════════════
    // 2D 物理
    // ════════════════════════════════════════════

    std::shared_ptr<IPhysicsWorld> PhysicsSystemManager::CreateWorld2D(const Vec2& gravity) {
        // 使用工厂模式创建 Box2D 世界（Box2DPhysicsWorld）
        // 该函数由具体的 Box2D 实现提供
        // 目前引擎的 Application 构造函数中已集成了 Box2D 世界的创建
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
    // 3D 物理（桩实现）
    // ════════════════════════════════════════════

    /// 3D 物理世界的空桩实现（占位）
    class NullPhysicsWorld3D : public IPhysicsWorld3D {
    public:
        NullPhysicsWorld3D(const Vec3& gravity) : m_Gravity(gravity) {}

        bool Init(const PhysicsWorldConfig3D&) override { return true; }
        void Shutdown() override {}

        void Step(float32, int32) override {}

        std::shared_ptr<IPhysicsBody3D> CreateBody(const BodyDef3D&) override {
            Log::Warn("[NullPhysics3D] CreateBody called — no 3D physics engine loaded");
            return nullptr;
        }
        void DestroyBody(IPhysicsBody3D*) override {}

        std::shared_ptr<IJoint3D> CreateJoint(const JointDef3D&) override {
            return nullptr;
        }
        void DestroyJoint(IJoint3D*) override {}

        std::vector<RayCastResult3D> RayCast(const Vec3&, const Vec3&) override {
            return {};
        }
        std::vector<IPhysicsBody3D*> QueryAABB(const Vec3&, const Vec3&) override {
            return {};
        }
        std::vector<IPhysicsBody3D*> QuerySphere(const Vec3&, float32) override {
            return {};
        }

        void SetGravity(const Vec3& g) override { m_Gravity = g; }
        Vec3 GetGravity() const override { return m_Gravity; }

        void SetContactBeginCallback(ContactCallback3D) override {}
        void SetContactEndCallback(ContactCallback3D) override {}
        void SetContactPreSolveCallback(ContactFilterCallback3D) override {}
        void SetContactPersistCallback(ContactPersistCallback3D) override {}

        void SetDebugDraw(IPhysicsDebugDraw3D*) override {}
        void DebugDraw() override {}

        Stats GetStats() const override { return {}; }
        void* GetNativeWorld() override { return nullptr; }

    private:
        Vec3 m_Gravity = {0, -9.81f, 0};
    };

    std::shared_ptr<IPhysicsWorld3D> PhysicsSystemManager::CreateWorld3D(
        const PhysicsWorldConfig3D& config)
    {
        // 返回空桩实现
        auto world = std::make_shared<NullPhysicsWorld3D>(config.gravity);
        world->Init(config);
        m_World3D = world;
        Log::Info("[Physics3D] Created NullPhysicsWorld3D stub. "
                  "Replace with JoltPhysicsWorld when integrating Jolt Physics.");
        return world;
    }

    void PhysicsSystemManager::SetWorld3D(std::shared_ptr<IPhysicsWorld3D> world) {
        m_World3D = std::move(world);
    }

    const char* PhysicsSystemManager::GetEngineName3D() const {
        if (dynamic_cast<NullPhysicsWorld3D*>(m_World3D.get()))
            return "None (stub)";
        return "Unknown (custom implementation)";
    }

    // ════════════════════════════════════════════
    // 统一步进
    // ════════════════════════════════════════════

    void PhysicsSystemManager::StepAll(float32 dt) {
        if (m_World2D) {
            m_World2D->Step(dt);
        }
        if (m_World3D) {
            m_World3D->Step(dt);
        }
    }

} // namespace Engine
