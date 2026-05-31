#pragma once

/**
 * @file PhysicsSystemManager.h
 * @brief 物理系统管理器 — 统一管理 2D 和 3D 物理世界
 *
 * 允许应用同时使用：
 *   - 2D 物理（Box2D）用于精灵/平台游戏
 *   - 3D 物理（Jolt/PhysX/Bullet）用于 3D 场景
 *
 * 管理器的职责：
 *   1. 持有物理世界的生命周期
 *   2. 统一 Step() 调度
 *   3. 提供工厂方法创建物理世界
 *   4. 调试统计汇总
 */

#include "Engine/Core/Physics/IPhysicsWorld.h"
#include "Engine/Core/Physics/IPhysicsWorld3D.h"
#include "Engine/Core/Physics/PhysicsDefs.h"
#include "Engine/Core/Physics/PhysicsDefs3D.h"
#include <memory>

namespace Engine {

    class PhysicsSystemManager {
    public:
        PhysicsSystemManager() = default;
        ~PhysicsSystemManager() = default;

        PhysicsSystemManager(const PhysicsSystemManager&) = delete;
        PhysicsSystemManager& operator=(const PhysicsSystemManager&) = delete;

        // ════════════════════════════════════════════
        // 2D 物理（现有 Box2D 接口）
        // ════════════════════════════════════════════

        /**
         * @brief 创建 2D 物理世界
         * @param gravity 重力向量 (默认 -9.81 Y)
         * @return IPhysicsWorld 实例（当前为 Box2D 实现）
         */
        std::shared_ptr<IPhysicsWorld> CreateWorld2D(const Vec2& gravity = {0, -9.81f});

        /**
         * @brief 使用自定义实现创建 2D 物理世界
         * @param world 外部创建的 IPhysicsWorld 实例
         */
        void SetWorld2D(std::shared_ptr<IPhysicsWorld> world);

        std::shared_ptr<IPhysicsWorld> GetWorld2D() const { return m_World2D; }

        // ════════════════════════════════════════════
        // 3D 物理（Jolt / PhysX / Bullet）
        // ════════════════════════════════════════════

        /**
         * @brief 创建 3D 物理世界（默认重力 -9.81 Y）
         *
         * 当前返回一个空桩实现（占位）。
         * 接入 Jolt Physics 后应替换为 JoltPhysicsWorld。
         *
         * 接入步骤（详见 docs/3rdparty/jolt-integration.md）：
         *   1. git submodule add https://github.com/jrouwe/JoltPhysics.git third_party/jolt
         *   2. 在 third_party/CMakeLists.txt 中添加 Jolt 构建
         *   3. 创建 engine/src/Jolt/JoltPhysicsWorld.cpp 实现 IPhysicsWorld3D
         */
        std::shared_ptr<IPhysicsWorld3D> CreateWorld3D(const PhysicsWorldConfig3D& config = {});

        /**
         * @brief 使用自定义实现设置 3D 物理世界
         * @param world 外部创建的 IPhysicsWorld3D 实例
         */
        void SetWorld3D(std::shared_ptr<IPhysicsWorld3D> world);

        std::shared_ptr<IPhysicsWorld3D> GetWorld3D() const { return m_World3D; }

        // ════════════════════════════════════════════
        // 统一步进
        // ════════════════════════════════════════════

        /**
         * @brief 步进所有已激活的物理世界
         * @param dt 时间步长
         */
        void StepAll(float32 dt);

        // ════════════════════════════════════════════
        // 查询
        // ════════════════════════════════════════════

        bool HasWorld2D() const { return m_World2D != nullptr; }
        bool HasWorld3D() const { return m_World3D != nullptr; }

        /**
         * @brief 获取物理引擎名称
         * @return 例如 "Box2D 2.4.1", "Jolt Physics 5.3"
         */
        const char* GetEngineName2D() const;
        const char* GetEngineName3D() const;

    private:
        std::shared_ptr<IPhysicsWorld>   m_World2D;
        std::shared_ptr<IPhysicsWorld3D> m_World3D;
    };

} // namespace Engine
