#pragma once

/**
 * @file IPhysicsWorld.h
 * @brief 物理世界接口 — 不依赖任何第三方物理库
 *
 * 设计原则（与 IRenderQueue 等 RHI 接口一致）：
 *   - 纯虚接口，引擎核心只依赖此抽象
 *   - Box2D 实现层提供具体实现
 *   - 所有方法使用 Engine::Vec2 / BodyDef，不暴露 Box2D 类型
 */

#include "Engine/Core/Physics/PhysicsDefs.h"
#include <memory>
#include <vector>

namespace Engine {

    class IPhysicsBody;

    class IPhysicsWorld {
    public:
        virtual ~IPhysicsWorld() = default;

        // ── 步进 ──
        /**
         * @brief 模拟物理世界
         * @param dt 时间步长（秒），建议固定为 1/60
         * @param velocityIterations 速度迭代次数（默认 8）
         * @param positionIterations 位置迭代次数（默认 3）
         */
        virtual void Step(float32 dt,
                          int32 velocityIterations = 8,
                          int32 positionIterations = 3) = 0;

        virtual std::shared_ptr<IPhysicsBody> CreateBody(const BodyDef& def) = 0;
        virtual void DestroyBody(IPhysicsBody* body) = 0;

        // ── 查询 ──
        /**
         * @brief 光线投射
         * @return 按距离排序的所有被击中的物体
         */
        virtual std::vector<RayCastResult> RayCast(const Vec2& from,
                                                     const Vec2& to) = 0;

        /**
         * @brief 范围查询（AABB）
         * @param center 查询中心
         * @param halfSize 半宽半高
         * @return 包围盒与查询区域重叠的所有物体
         */
        virtual std::vector<IPhysicsBody*> QueryAABB(const Vec2& center,
                                                       const Vec2& halfSize) = 0;

        virtual void SetGravity(const Vec2& gravity) = 0;
        virtual Vec2 GetGravity() const = 0;
─
        virtual void SetContactBeginCallback(ContactCallback callback) = 0;
        virtual void SetContactEndCallback(ContactCallback callback) = 0;
        virtual void SetContactPreSolveCallback(ContactFilterCallback callback) = 0;

        virtual void DebugDraw() = 0;

        virtual void* GetNativeWorld() = 0;
    };

} // namespace Engine
