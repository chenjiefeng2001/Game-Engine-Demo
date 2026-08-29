#pragma once

/**
 * @file ComponentRegistry_Go.h
 * @brief Component Contract 的组件工厂注册表（GameObject 模型，F1-A）
 *
 * 契约：组件的持久化 / Lua / Editor 身份 = 稳定字符串类型名（GetComponentTypeName）。
 * 本表把“稳定类型名 → 构造工厂”建立映射，使 AddComponentByName /
 * 序列化还原 / 脚本创建 三处共享同一身份，杜绝依赖 typeid/class name。
 *
 * 约定：
 *   - 只有“已收编”为契约组件的类型才注册（在 .cpp 侧调用 Register）。
 *   - Create() 每次调用都会确保内置契约组件已注册（EnsureRegistered 幂等）。
 *   - 未知类型返回 nullptr（契约负路径）。
 */

#include <functional>
#include <memory>
#include <string>

namespace Engine {
    class Component;

namespace ComponentRegistryGo {

    using Factory = std::function<std::shared_ptr<Component>()>;

    /// 注册一个契约组件类型（幂等：重复注册以后者为准，但会告警）
    void Register(const std::string& typeName, Factory factory);

    /// 是否已注册该稳定类型名
    bool IsRegistered(const std::string& typeName);

    /// 按稳定类型名创建组件实例；未知类型 / 未注册返回 nullptr
    std::shared_ptr<Component> Create(const std::string& typeName);

    /// 已注册的契约组件类型数量
    size_t Count();

    /// 确保内置契约组件（Camera 等）已注册（Create 内部自动调用，幂等）
    void EnsureRegistered();

} // namespace ComponentRegistryGo
} // namespace Engine