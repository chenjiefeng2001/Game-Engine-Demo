#pragma once

/**
 * @file ColliderComponent.h
 * @brief F2-A Collider 参考组件 —— 第二个“四面向全通”契约组件（声明式上卷）
 *
 * Collider 是跨 Physics/ECS 边界的实证对象，验证 F1 模板不是 Camera 特例。
 *
 * 【所有者模型（F2 裁定）】
 *   - ColliderComponent 只拥有【声明式稳定状态】：shape / radius / halfX / halfY /
 *     isSensor / category / mask / enabled。
 *   - 【不持有】任何 Physics 内部对象：Jolt BodyID · ShapeRef · ECS Entity ID ·
 *     Physics world 指针 · 帧局部句柄 —— 这些是 runtime implementation detail，
 *     由 F2-B 的 PhysicsColliderAdapter 持有，绝不当持久化身份。
 *   - 【不 #include】PhysicsDefs.h：本组件与 Physics 表示解耦，shape 以稳定字符串
 *     身份表达（与持久化/Lua/Editor 共用），Physics 映射交给 Adapter。
 *   - 空间状态（世界位置）归 Entity Transform 一方所有，本组件不复制。
 *   - 方向：Scene/Component State → Physics Runtime（派生状态），反之不成立。
 *
 * 形状（F2 范围收敛）：仅 Circle（radius）与 Box（halfX/halfY）。
 * Edge/Chain/Polygon 属现有 Physics 能力，但不在 F2 Collider 契约内（避免范围蔓延）。
 *
 * Persistence identity：GetComponentTypeName() == "Collider"（稳定，与 Camera 同源）。
 */

#include "Engine/Core/GameObject/Component.h"
#include <string>

namespace Engine {

    /// Collider 形状身份（稳定字符串，跨持久化/Lua/Editor/Adapter 共用）
    enum class ColliderShape : uint8 {
        Circle = 0,
        Box    = 1
    };

    /// 稳定形状名（"Circle"/"Box"）；未知返回 nullptr
    const char* ColliderShapeToString(ColliderShape shape);
    /// 由稳定名解析形状；未知返回 false
    bool ParseColliderShape(const char* name, ColliderShape* out);

    class ColliderComponent : public Component {
    public:
        ColliderComponent() = default;
        ~ColliderComponent() override = default;

        const char* GetTypeDisplayName() const override { return "Collider"; }
        // 契约稳定身份（F1-A 同源）
        const char* GetComponentTypeName() const override { return "Collider"; }

        // ── 配置访问器（声明式状态）──
        void SetShape(ColliderShape s) { m_Shape = s; }
        ColliderShape GetShape() const { return m_Shape; }

        void SetRadius(float r) { m_Radius = r; }
        float GetRadius() const { return m_Radius; }

        void SetHalfExtents(float hx, float hy) { m_HalfX = hx; m_HalfY = hy; }
        void SetHalfExtentsX(float hx) { m_HalfX = hx; }
        void SetHalfExtentsY(float hy) { m_HalfY = hy; }
        float GetHalfExtentsX() const { return m_HalfX; }
        float GetHalfExtentsY() const { return m_HalfY; }

        /// 传感器标记（只触发回调，不产生实体碰撞）
        void SetIsSensor(bool s) { m_IsSensor = s; }
        bool IsSensor() const { return m_IsSensor; }

        /// 碰撞滤波（与 2D BodyDef categoryBits/maskBits 语义一致，但不依赖其头）
        void SetCategory(uint16 c) { m_Category = c; }
        uint16 GetCategory() const { return m_Category; }
        void SetMask(uint16 m) { m_Mask = m; }
        uint16 GetMask() const { return m_Mask; }

        // ── 反射 / 元数据（F1-B 同源）──
        size_t GetPropertyCount() const override;
        bool GetPropertyDesc(size_t index, ComponentPropertyDesc* out) const override;
        bool GetPropertyValue(size_t index, ComponentPropertyValue* out) const override;
        bool SetPropertyValue(size_t index, const ComponentPropertyValue& value) override;

        // ── 序列化（F1-D 同源：组件化快照 components[]）──
        void Serialize(nlohmann::json& json) const override;
        bool Deserialize(const nlohmann::json& json) override;

    private:
        ColliderShape m_Shape    = ColliderShape::Circle;
        float         m_Radius   = 0.5f;   ///< Circle
        float         m_HalfX    = 0.5f;   ///< Box 半宽
        float         m_HalfY    = 0.5f;   ///< Box 半高
        bool          m_IsSensor = false;
        uint16        m_Category = 0x0001; ///< Layer_Default（对齐 CollisionLayers）
        uint16        m_Mask     = 0xFFFF; ///< Layer_All
    };

    /// 登记内置契约组件（Collider）到 ComponentRegistryGo；由 EnsureRegistered 调用
    void RegisterColliderComponent();

} // namespace Engine