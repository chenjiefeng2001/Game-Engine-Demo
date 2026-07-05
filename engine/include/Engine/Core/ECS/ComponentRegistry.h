#pragma once

/**
 * @file ComponentRegistry.h
 * @brief 全局组件元信息注册表
 *
 * 每个组件类型在使用前必须注册。
 * 注册在程序初始化时（static init）自动完成。
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

/** 内部：将元信息加入注册表（由 RegisterComponentType 调用） */
void RegisterMeta(const ComponentMeta& meta);

} // namespace Engine