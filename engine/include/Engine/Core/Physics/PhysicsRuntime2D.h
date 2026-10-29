#pragma once

/**
 * @file PhysicsRuntime2D.h
 * @brief T-1-C：GameObject 生产路径的 2D 物理运行时
 *
 * 所有权：本对象拥有 Box2D World 与 PhysicsColliderAdapter；EditorSession 的
 * Play/Stop 管理本对象的生命周期。头文件不暴露 Box2D 类型。
 *
 * 步进：固定 1/60s，最多 8 个子步；输入 realDt 被钳制到 0.25s。
 * 同步：Kinematic 由 Transform → Body；Dynamic/Kinematic 在步进后 Body → Transform。
 */

#include "Engine/Core/Physics/PhysicsDefs.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

    class Scene;
    class GameObject;
    class IPhysicsWorld;
    class PhysicsColliderAdapter;

    class PhysicsRuntime2D {
    public:
        static constexpr float32 kFixedDt = 1.0f / 60.0f;
        static constexpr int32 kMaxSubSteps = 8;
        static constexpr float32 kMaxFrameDt = 0.25f;

        PhysicsRuntime2D();
        ~PhysicsRuntime2D();

        PhysicsRuntime2D(const PhysicsRuntime2D&) = delete;
        PhysicsRuntime2D& operator=(const PhysicsRuntime2D&) = delete;

        /// 创建 World、观察场景对象并按 RigidBody+Collider 建体。
        bool Start(Scene& scene, std::string* error = nullptr);

        /// 销毁全部 body/World 并清除观察到对象上的变更钩子。
        void Stop();

        /// 固定步长推进；失败时 error 提供诊断且不继续使用无效 World。
        bool Step(float32 realDt, std::string* error = nullptr);

        bool IsRunning() const noexcept { return m_Running; }
        size_t GetBodyCount() const;
        size_t GetObservedObjectCount() const { return m_Observed.size(); }
        const std::string& GetLastError() const noexcept { return m_LastError; }

    private:
        void RefreshObservations();
        void SyncKinematicBodies();
        void SyncPhysicsToTransforms();

        std::shared_ptr<IPhysicsWorld> m_World;
        std::unique_ptr<PhysicsColliderAdapter> m_Adapter;
        Scene* m_Scene = nullptr;
        std::vector<std::weak_ptr<GameObject>> m_Observed;
        float32 m_Accumulator = 0.0f;
        std::string m_LastError;
        bool m_Running = false;
    };

} // namespace Engine
