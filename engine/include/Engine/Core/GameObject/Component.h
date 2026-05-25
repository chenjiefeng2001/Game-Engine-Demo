#pragma once

/**
 * @file Component.h
 * @brief 组件基类 — 所有可挂载到 GameObject 上的组件派生自此类
 *
 * 设计原则（与 Unity 的 Component 模型一致）：
 *   - 每个组件有 OnCreate / OnUpdate / OnDestroy 生命周期钩子
 *   - 组件可以挂载渲染数据（CollectRenderCommands）
 *   - 组件不拥有自己的生命周期——由 GameObject 决定何时销毁
 *
 * 使用示例：
 * @code
 *   class HealthComponent : public Component {
 *       int hp = 100;
 *       void OnUpdate(float dt) override {
 *           if (hp <= 0) GetOwner()->SetActive(false);
 *       }
 *   };
 *
 *   auto obj = std::make_shared<GameObject>("Enemy");
 *   auto health = obj->AddComponent<HealthComponent>();
 *   // 此时 health 是一个 std::shared_ptr<HealthComponent>
 *   // 通过 obj->GetComponent<HealthComponent>() 可随时访问
 * @endcode
 */

#include "Engine/Types.h"

namespace Engine {

    class GameObject;

    class Component {
    public:
        Component() = default;
        virtual ~Component() = default;

        Component(const Component&) = delete;
        Component& operator=(const Component&) = delete;

        // ── 生命周期 ──
        /** 对象被添加到场景后调用（保证所有组件就绪后触发） */
        virtual void OnCreate() {}
        /** 每帧更新逻辑 */
        virtual void OnUpdate(float32 dt) { (void)dt; }
        /** 对象被销毁时调用 */
        virtual void OnDestroy() {}

        // ── 渲染（可选） ──
        /**
         * @brief 收集本组件的渲染命令到队列（RHI 原则：只提交纯数据）
         * @param queue 渲染命令队列
         *
         * 由 GameObject::CollectRenderCommands 遍历所有组件时调用。
         * 非渲染组件不需要重写此方法。
         */
        virtual void CollectRenderCommands(class IRenderQueue& queue) { (void)queue; }

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