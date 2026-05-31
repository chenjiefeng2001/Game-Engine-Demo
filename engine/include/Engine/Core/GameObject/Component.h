#pragma once

/**
 * @file Component.h
 * @brief 组件基类 — 所有可挂载到 GameObject 上的组件派生自此类
 *
 * 设计原则（与 Unity 的 Component 模型一致）：
 *   - 每个组件有 OnCreate / OnUpdate / OnDestroy 生命周期钩子
 *   - 组件可以挂载渲染数据（CollectRenderCommands）
 *   - 组件不拥有自己的生命周期——由 GameObject 决定何时销毁
 *   - 序列化：实现 Serialize/Deserialize 后自动纳入场景保存/加载
 *
 * 使用示例：
 * @code
 *   class HealthComponent : public Component {
 *       int hp = 100;
 *       void OnUpdate(float dt) override {
 *           if (hp <= 0) GetOwner()->SetActive(false);
 *       }
 *       void Serialize(nlohmann::json& j) const override { j["hp"] = hp; }
 *       bool Deserialize(const nlohmann::json& j) override { return j.contains("hp"); }
 *   };
 * @endcode
 */

#include "Engine/Types.h"
#include <nlohmann/json.hpp>

namespace Engine {

    class GameObject;

    class Component {
    public:
        Component() = default;
        virtual ~Component() = default;

        Component(const Component&) = delete;
        Component& operator=(const Component&) = delete;

        // 允许移动语义（派生类如 AudioSourceComponent/PhysicsComponent 需要）
        Component(Component&&) noexcept = default;
        Component& operator=(Component&&) noexcept = default;

        // ── 生命周期 ──
        virtual void OnCreate() {}
        virtual void OnUpdate(float32 dt) { (void)dt; }
        virtual void OnDestroy() {}

        // ── 渲染 ──
        virtual void CollectRenderCommands(class IRenderQueue& queue) { (void)queue; }

        // ── 序列化（重写后自动纳入场景保存/加载） ──
        /** 将组件数据写入 JSON 对象 */
        virtual void Serialize(nlohmann::json& json) const { (void)json; }
        /** 从 JSON 对象读取组件数据 */
        virtual bool Deserialize(const nlohmann::json& json) { (void)json; return true; }

        // ── 所属对象 ──
        /** 获取挂载此组件的 GameObject */
        GameObject* GetOwner() noexcept { return m_Owner; }
        const GameObject* GetOwner() const noexcept { return m_Owner; }

        /** 组件是否已启用 */
        bool IsEnabled() const noexcept { return m_Enabled; }
        void SetEnabled(bool enabled) noexcept { m_Enabled = enabled; }

    private:
        friend class GameObject;
        GameObject* m_Owner = nullptr;
        bool m_Enabled = true;
    };

} // namespace Engine