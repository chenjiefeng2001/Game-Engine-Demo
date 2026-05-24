#pragma once

/**
 * @file IPhysicsDebugDraw.h
 * @brief 物理调试绘制接口 — 由引擎渲染层实现
 *
 * 设计原则：
 *   - 纯虚接口，不依赖任何第三方库
 *   - 所有坐标使用 Engine::Vec2，与 RHI/MathTypes.h 一致
 *   - 引擎渲染层（如 OpenGL）实现此接口完成实际绘制
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"

namespace Engine {

// ============================================================
// 调试绘制颜色
// ============================================================
struct DebugColor {
    float32 r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    DebugColor() = default;
    DebugColor(float32 r_, float32 g_, float32 b_, float32 a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}
};

/// 调试绘制 flags（与 b2Draw::DrawFlags 对应）
enum DebugDrawFlags : uint32 {
    DebugDraw_None      = 0,
    DebugDraw_Shape     = 1 << 0,   ///< 绘制形状
    DebugDraw_Joint     = 1 << 1,   ///< 绘制关节
    DebugDraw_AABB      = 1 << 2,   ///< 绘制包围盒
    DebugDraw_COM       = 1 << 3,   ///< 绘制质心
    DebugDraw_All       = 0xFFFFFFFF
};

// ============================================================
// 调试绘制接口
// ============================================================
class IPhysicsDebugDraw {
public:
    virtual ~IPhysicsDebugDraw() = default;

    virtual void DrawPolygon(const Vec2* vertices, int32 vertexCount,
                             const DebugColor& color) = 0;
    virtual void DrawSolidPolygon(const Vec2* vertices, int32 vertexCount,
                                  const DebugColor& color) = 0;
    virtual void DrawCircle(const Vec2& center, float32 radius,
                            const DebugColor& color) = 0;
    virtual void DrawSolidCircle(const Vec2& center, float32 radius,
                                 const Vec2& axis, const DebugColor& color) = 0;
    virtual void DrawSegment(const Vec2& p1, const Vec2& p2,
                             const DebugColor& color) = 0;
    virtual void DrawTransform(const Vec2& position, float32 angle) = 0;
    virtual void DrawPoint(const Vec2& p, float32 size,
                           const DebugColor& color) = 0;

    /// 设置调试绘制的 flags（位掩码，参考 DebugDrawFlags）
    virtual void SetFlags(uint32 flags) = 0;
    virtual uint32 GetFlags() const = 0;
};

} // namespace Engine
