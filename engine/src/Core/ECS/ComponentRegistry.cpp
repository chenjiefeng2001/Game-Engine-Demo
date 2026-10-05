#include "Engine/Core/ECS/ComponentRegistry.h"
#include "Engine/Core/ECS/PhysicsComponents.h"
#include <cassert>

namespace Engine {

namespace {
    struct ComponentRegistry {
        std::unordered_map<ComponentTypeID, ComponentMeta> metas;
    };

    ComponentRegistry& GetRegistry() {
        static ComponentRegistry reg;
        return reg;
    }
}

ComponentMeta* GetComponentMetaByTypeID(ComponentTypeID typeID) {
    auto& reg = GetRegistry();
    auto it = reg.metas.find(typeID);
    if (it != reg.metas.end()) {
        return &it->second;
    }
    return nullptr;
}

void RegisterMeta(const ComponentMeta& meta) {
    auto& reg = GetRegistry();
    auto it = reg.metas.find(meta.typeID);
    if (it == reg.metas.end()) {
        reg.metas[meta.typeID] = meta;
    }
}

void InitializeComponentRegistry() {
    // 内置 ECS 组件类型。必须在此处集中注册：任何在文件级或函数级 static
    // 初始化中注册的做法都会重新引入对 C++ static initialization order 的
    // 依赖，已被 B4 决策排除。
    RegisterComponentType<RigidBody3DComponent>();
    RegisterComponentType<PhysicsRuntimeComponent>();
    RegisterComponentType<BoxCollider3DComponent>();
    RegisterComponentType<SphereCollider3DComponent>();
    RegisterComponentType<CapsuleCollider3DComponent>();

    // 与上面同属 PhysicsComponents.h，此前被遗漏；未注册时
    // PhysicsSyncSystem 取到的指针恒为 null，joint 清理逻辑从不执行。
    RegisterComponentType<Joint3DComponent>();
}

} // namespace Engine