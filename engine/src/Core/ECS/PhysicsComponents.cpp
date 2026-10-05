/**
 * @file PhysicsComponents.cpp
 * @brief ECS 3D 物理组件类型定义
 *
 * 所有 3D 物理相关的 ECS 组件类型声明于 PhysicsComponents.h。
 * 注册不再在此处发生：见 ComponentRegistry.cpp 的
 * InitializeComponentRegistry()，由启动路径显式调用。
 */

#include "Engine/Core/ECS/PhysicsComponents.h"

namespace Engine {

// 3D 物理组件类型的注册已移至 ComponentRegistry.cpp 的
// InitializeComponentRegistry()，由启动路径显式调用，不再依赖 static init。

} // namespace Engine