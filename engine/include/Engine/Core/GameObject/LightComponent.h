#pragma once

/**
 * @file LightComponent.h
 * @brief 光照组件 — 方向光/点光源数据
 */

#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

    enum class LightType : uint8 {
        Directional,
        Point
    };

    class LightComponent : public Component {
    public:
        LightComponent() = default;

        const char* GetTypeDisplayName() const override { return "Light"; }

        LightType Type      = LightType::Directional;
        Vec3      Color     = { 1.0f, 1.0f, 1.0f };
        float     Intensity = 1.0f;
        float     Radius    = 10.0f; // 仅对 Point Light 生效
    };

} // namespace Engine