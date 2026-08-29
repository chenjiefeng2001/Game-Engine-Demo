#pragma once

/**
 * @file CameraComponent.h
 * @brief F1-C Camera 参考组件 —— 第一个“四面向全通”的契约组件
 *
 * 证明：Runtime(state) + Serialization + Lua + Editor/Reflection 四线统一贯通。
 * 这是 Component Contract v1 的试金石（F1-Camera Vertical Slice）。
 *
 * 最小 Camera（第一版刻意不加 Cinemachine / camera stack / post-processing / shake）：
 *   - Enabled   → 复用 Component::SetEnabled/IsEnabled（启停 = 生命周期）
 *   - Zoom      → 缩放倍率
 *   - Viewport  → 视口矩形，字符串 "x y w h"
 *   - Bounds    → 相机世界轴对齐范围，字符串 "x0 y0 x1 y1"（空 = 无约束）
 *
 * Persistence identity：GetComponentTypeName() == "Camera"（稳定），
 * 序列化走 Component::Serialize/Deserialize，反射走 GetProperty*。
 */

#include "Engine/Core/GameObject/Component.h"
#include <string>

namespace Engine {

    class OrthographicCamera;

    class CameraComponent : public Component {
    public:
        CameraComponent() = default;
        ~CameraComponent() override = default;

        const char* GetTypeDisplayName() const override { return "Camera"; }
        // Contract 稳定身份（F1-A）
        const char* GetComponentTypeName() const override { return "Camera"; }

        // ── 配置访问器 ──
        void SetZoom(float z) { m_Zoom = z; }
        float GetZoom() const { return m_Zoom; }

        void SetViewport(const std::string& v) { m_Viewport = v; }
        const std::string& GetViewport() const { return m_Viewport; }

        void SetBounds(const std::string& b) { m_Bounds = b; }
        const std::string& GetBounds() const { return m_Bounds; }

        // ── 反射 / 元数据（F1-B）──
        size_t GetPropertyCount() const override { return 4; }
        bool GetPropertyDesc(size_t index, ComponentPropertyDesc* out) const override;
        bool GetPropertyValue(size_t index, ComponentPropertyValue* out) const override;
        bool SetPropertyValue(size_t index, const ComponentPropertyValue& value) override;

        // ── 序列化（F1-D）──
        void Serialize(nlohmann::json& json) const override;
        bool Deserialize(const nlohmann::json& json) override;

        // ── Runtime（F1-C）：由组件状态构造真实正交相机（应用 zoom/bounds）──
        OrthographicCamera MakeOrthographic(float aspectRatio) const;

    private:
        float       m_Zoom    = 1.0f;
        std::string m_Viewport = "0 0 1280 720";
        std::string m_Bounds   = "";
    };

    /// 注册内置契约组件（Camera）到 ComponentRegistryGo；由 EnsureRegistered 调用
    void RegisterCameraComponent();

} // namespace Engine