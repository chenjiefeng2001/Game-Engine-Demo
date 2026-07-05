#pragma once

/**
 * @file ECSBridge.h
 * @brief ECS ↔ GameObject 兼容桥接层
 *
 * 在混合模式下运行时，GameObject 内部持有一个 EntityHandle，
 * 所有组件操作转发到 ECS EntityManager。
 *
 * 桥接方案：
 *   1. 每个 GameObject 拥有一个 EntityHandle
 *   2. AddComponent<T> 委托给 EntityManager::AddComponent<T>
 *   3. 保留 m_Components unordered_map 用于编辑器反射/序列化
 *   4. TransformComponent 保持内联（不使用 ECS 存储）
 */

#include "Engine/Core/ECS/ECS.fwd.h"
#include <unordered_map>
#include <memory>

namespace Engine {

class GameObject;
class Component;

class ECSBridge {
public:
    /** 获取 GameObject 对应的 EntityHandle */
    static EntityHandle GetEntityHandle(GameObject* go);

    /** 将 EntityHandle 关联到 GameObject */
    static void SetEntityHandle(GameObject* go, EntityHandle entity);

    /** 检查 Entity 是否有关联的 GameObject */
    static bool HasGameObject(EntityHandle entity);

    /** 获取 Entity 对应的 GameObject（若存在） */
    static GameObject* GetGameObject(EntityHandle entity);

    /** 建立双向映射 */
    static void Link(GameObject* go, EntityHandle entity);

    /** 解除映射 */
    static void Unlink(GameObject* go);

private:
    struct BridgeData {
        std::unordered_map<uint64, GameObject*> entityToGO;
        std::unordered_map<uint64, EntityHandle> goToEntity;
    };

    static BridgeData& GetData();
};

} // namespace Engine