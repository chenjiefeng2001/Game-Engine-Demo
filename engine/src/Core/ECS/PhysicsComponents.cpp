/**
 * @file PhysicsComponents.cpp
 * @brief ECS 3D 物理组件注册 — 在 static init 阶段自动注册到 ComponentRegistry
 *
 * 所有 3D 物理相关的 ECS 组件的类型在此注册。
 * 注册后才能被 EntityManager 用于 Archetype 创建和 Query 匹配。
 */

#include "Engine/Core/ECS/PhysicsComponents.h"
#include "Engine/Core/ECS/ComponentRegistry.h"

namespace Engine {

// ── 静态注册：程序初始化时自动调用 ──
// 每个组件类型调用 RegisterComponentType<T>() 使其元信息进入全局注册表
static bool s_Registered = []() {
    RegisterComponentType<RigidBody3DComponent>();
    RegisterComponentType<PhysicsRuntimeComponent>();
    RegisterComponentType<BoxCollider3DComponent>();
    RegisterComponentType<SphereCollider3DComponent>();
    RegisterComponentType<CapsuleCollider3DComponent>();
    return true;
}();

} // namespace Engine