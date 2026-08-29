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

#include <string>

namespace Engine {

    class GameObject;

    // ── Component Contract 值类型（F1-A/B：稳定身份 + 反射/元数据）──
    // 反射允许 Inspector / Lua / 序列化经 metadata 读取组件，而不是依赖
    // C++ class 名 / typeid。契约组件的属性按注册表登记的字符串身份暴露。
    enum class ComponentValueType : uint8 {
        Bool = 0,
        Int,
        Float,
        String
    };

    /// 组件属性描述（供 Editor/Inspector 消费，避免其知道 C++ class）
    struct ComponentPropertyDesc {
        const char*       name     = nullptr;
        ComponentValueType type    = ComponentValueType::Float;
        bool              editable = true;   ///< 是否可在 Editor 作者编辑
    };

    /// 组件属性的统一值（按 type 取对应字段）
    struct ComponentPropertyValue {
        ComponentValueType type        = ComponentValueType::Float;
        bool               boolValue   = false;
        int32              intValue    = 0;
        float32            floatValue  = 0.0f;
        std::string        stringValue;
    };
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

        /** 获取组件的类型名称字符串（用于 UI 显示） */
        template<typename T>
        static const char* GetTypeName() {
            // 默认使用 typeid 名称（经 demangle 简化为可读名称）
            // 特化版本可返回更友好的名称
            return ParseTypeName(typeid(T).name());
        }

        /** 获取此实例的类型名称 */
        virtual const char* GetTypeDisplayName() const { return "Component"; }

        // ════════════════════════════════════════════════════════════
        // Component Contract v1（F1-A/B）—— 稳定身份 + 反射/元数据
        //   - 契约组件覆盖 GetComponentTypeName() 返回稳定字符串（持久化/脚本
        //     身份，绝不依赖 typeid / class name / UI 字符串）。
        //   - 默认实现为空：只有“已收编”的契约组件暴露属性，避免破坏既有
        //     Sprite/Physics 等非契约组件的现状（F2 逐步收编）。
        // ════════════════════════════════════════════════════════════

        /** 稳定契约类型名（持久化 + Lua + Editor 身份）。
         *  返回 nullptr = 该组件尚未收编进 Contract（不外露/不序列化为 components[]）。 */
        virtual const char* GetComponentTypeName() const { return nullptr; }

        /** 反射属性数量（默认 0 = 无反射属性） */
        virtual size_t GetPropertyCount() const { return 0; }

        /** 第 index 个属性的描述（越界返回 false） */
        virtual bool GetPropertyDesc(size_t index, ComponentPropertyDesc* out) const {
            (void)index; (void)out; return false;
        }

        /** 读第 index 个属性的值（越界返回 false） */
        virtual bool GetPropertyValue(size_t index, ComponentPropertyValue* out) const {
            (void)index; (void)out; return false;
        }

        /** 写第 index 个属性的值（越界 / 类型不符返回 false） */
        virtual bool SetPropertyValue(size_t index, const ComponentPropertyValue& value) {
            (void)index; (void)value; return false;
        }

        /** 解析编译器 typeid name 为可读字符串（公开工具方法） */
        static const char* ParseTypeName(const char* mangledName);

    private:
        friend class GameObject;
        GameObject* m_Owner = nullptr;
        bool m_Enabled = true;
    };

} // namespace Engine