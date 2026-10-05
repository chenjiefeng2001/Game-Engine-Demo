#pragma once

/**
 * @file ComponentRegistry.h
 * @brief 全局组件元信息注册表
 *
 * 每个组件类型都必须在使用前完成注册。
 * 注册不会在 static initialization 阶段自动发生：由启动路径显式调用
 * InitializeComponentRegistry() 完成，且该调用必须在任何 EntityManager
 * 使用该类型之前抵达。
 */

#include "Engine/Core/ECS/ECS.fwd.h"
#include <unordered_map>

namespace Engine {

/** 根据组件类型 ID 查找注册的元信息 */
ComponentMeta* GetComponentMetaByTypeID(ComponentTypeID typeID);

/** 注册组件类型（会自动将 MakeComponentMeta<T>() 加入注册表） */
template<typename T>
void RegisterComponentType() {
    ComponentTypeID id = ComponentType<T>::ID();
    ComponentMeta* existing = GetComponentMetaByTypeID(id);
    if (!existing) {
        // 内部注册表在此添加
        RegisterMeta(MakeComponentMeta<T>());
    }
}

/** 注册内置组件类型的初始化边界。
 *
 *  这是 B4 决策指定的唯一 registration 入口，需由启动路径在创建
 *  EntityManager / 添加组件之前显式调用。
 *
 *  - **幂等**：可重复调用，重复调用不会改变已注册集合。
 *  - **不做自动注册**：调用之后的首次使用**不会**触发注册；未注册类型仍按
 *    629ea47 的语义产生可诊断失败。
 *  - **不依赖 static initialization order**：注册工作全部在本函数体内完成，
 *    不使用文件级或函数级 static 初始化副作用。
 */
void InitializeComponentRegistry();

/** 内部：将元信息加入注册表（由 RegisterComponentType 调用） */
void RegisterMeta(const ComponentMeta& meta);

} // namespace Engine