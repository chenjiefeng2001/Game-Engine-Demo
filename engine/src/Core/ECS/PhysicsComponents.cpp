/**
 * @file PhysicsComponents.cpp
 * @brief ECS 3D 物理组件注册 — 在 static init 阶段自动注册到 ComponentRegistry
 *
 * 所有 3D 物理相关的 ECS 组件的类型在此注册。
 * 注册后才能被 EntityManager 用于 Archetype 创建和 Query 匹配。
 */

#include "Engine/Core/ECS/PhysicsComponents.h"

namespace Engine {

// 3D 物理组件类型的注册已移至 ComponentRegistry.cpp 的
// InitializeComponentRegistry()，由启动路径显式调用，不再依赖 static init。

} // namespace Engine