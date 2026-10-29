#pragma once

/**
 * @file RigidBodyComponent.h
 * @brief T-1-A RigidBody 契约（2D Box2D 生产主线）
 *
 * 所有权边界（Gate 0 G-2）：
 *   - RigidBody 只拥有刚体级模拟配置：motionType / linearDamping / angularDamping。
 *   - 几何、sensor、category/mask 归 Collider；世界位置/旋转归 GameObject Transform。
 *   - mass 由 Box2D 从 Collider 几何与 fixture density 计算，不在本契约重复存储。
 *   - 不持有 IPhysicsBody / BodyID / World / ECS Entity 等运行时句柄。
 *
 * T-1-A 只交付契约与注册；序列化、物理绑定、Lua、Inspector 由 T-1-B..E 交付。
 * Persistence identity：GetComponentTypeName() == "RigidBody"。
 */

#include "Engine/Core/GameObject/Component.h"

#include <functional>

namespace Engine {

    /// 刚体运动类型（稳定字符串跨持久化/Lua/Editor/Adapter 共用）
    enum class RigidBodyMotionType : uint8 {
        Static    = 0,
        Kinematic = 1,
        Dynamic   = 2
    };

    /// 稳定名称（"Static"/"Kinematic"/"Dynamic"）；未知返回 nullptr
    const char* RigidBodyMotionTypeToString(RigidBodyMotionType type);
    /// 由稳定名称解析；未知/空指针返回 false
    bool ParseRigidBodyMotionType(const char* name, RigidBodyMotionType* out);

    class RigidBodyComponent : public Component {
    public:
        RigidBodyComponent() = default;
        ~RigidBodyComponent() override = default;

        const char* GetTypeDisplayName() const override { return "RigidBody"; }
        const char* GetComponentTypeName() const override { return "RigidBody"; }

        // ── 配置访问器（非法值拒绝，保持原值）──
        bool SetMotionType(RigidBodyMotionType type);
        RigidBodyMotionType GetMotionType() const { return m_MotionType; }

        bool SetLinearDamping(float damping);
        float GetLinearDamping() const { return m_LinearDamping; }

        bool SetAngularDamping(float damping);
        float GetAngularDamping() const { return m_AngularDamping; }

        /// 属性变更钩子（T-1-C）：由 PhysicsRuntime2D/Adapter 安装；组件本身无物理依赖。
        using RigidBodyMutationHook =
            std::function<void(RigidBodyComponent&, const char* propName)>;
        void SetMutationHook(RigidBodyMutationHook hook) {
            m_MutationHook = std::move(hook);
        }

        // ── 反射 / 元数据（F1 同源）──
        size_t GetPropertyCount() const override;
        bool GetPropertyDesc(size_t index, ComponentPropertyDesc* out) const override;
        bool GetPropertyValue(size_t index, ComponentPropertyValue* out) const override;
        bool SetPropertyValue(size_t index, const ComponentPropertyValue& value) override;

        // ── 序列化（T-1-B：只持久化三字段，不写运行时句柄）──
        void Serialize(nlohmann::json& json) const override;
        bool Deserialize(const nlohmann::json& json) override;

    private:
        void NotifyChanged(const char* propName);

        RigidBodyMotionType m_MotionType = RigidBodyMotionType::Dynamic;
        float m_LinearDamping  = 0.0f;
        float m_AngularDamping = 0.0f;
        RigidBodyMutationHook m_MutationHook;
    };

    /// 登记内置契约组件（RigidBody）到 ComponentRegistryGo；由 EnsureRegistered 调用
    void RegisterRigidBodyComponent();

} // namespace Engine
