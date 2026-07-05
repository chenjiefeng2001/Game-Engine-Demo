#pragma once

/**
 * @file System.h
 * @brief ECS System 基类 — 实现游戏逻辑的迭代器
 *
 * System 负责遍历匹配的实体，执行游戏逻辑。
 * 所有结构性变更必须通过 EntityCommandBuffer，禁止直接操作 EntityManager。
 *
 * 使用示例：
 * @code
 *   class MovementSystem : public ISystem {
 *       void Update(float32 dt, EntityManager& em, EntityCommandBuffer& ecb) override {
 *           auto query = em.Query().With<Transform, Velocity>().Build();
 *           for (auto [chunk, start, count] : query) {
 *               auto transforms = chunk->GetComponentSpan<Transform>();
 *               auto velocities = chunk->GetComponentSpan<Velocity>();
 *               for (uint32 i = start; i < start + count; ++i) {
 *                   transforms[i].position += velocities[i].value * dt;
 *               }
 *           }
 *       }
 *   };
 * @endcode
 */

#include "Engine/Core/ECS/ECS.fwd.h"

namespace Engine {

class ISystem {
public:
    virtual ~ISystem() = default;

    /** 系统名称（调试用） */
    virtual const char* GetName() const = 0;

    /** 每帧更新 */
    virtual void Update(float32 deltaTime, class EntityManager& em, class EntityCommandBuffer& ecb) = 0;

    /** 初始化（场景加载时调用） */
    virtual void OnInit() {}

    /** 销毁 */
    virtual void OnDestroy() {}
};

} // namespace Engine