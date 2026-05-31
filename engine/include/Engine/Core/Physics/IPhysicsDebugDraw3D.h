#pragma once

/**
 * @file IPhysicsDebugDraw3D.h
 * @brief 3D 物理调试绘制接口 — 绘制碰撞形状、约束等
 *
 * Jolt Physics 自带 DebugRenderer，PhysX 有 PVD。
 * 此接口允许引擎将物理调试信息渲染到自定义渲染器。
 */

#include "Engine/Core/RHI/MathTypes.h"
#include <cstdint>

namespace Engine {

    class IPhysicsDebugDraw3D {
    public:
        virtual ~IPhysicsDebugDraw3D() = default;

        // ── 绘制图元 ──
        virtual void DrawLine(const Vec3& from, const Vec3& to,
                              const Vec4& color) = 0;

        virtual void DrawTriangle(const Vec3& v0, const Vec3& v1, const Vec3& v2,
                                  const Vec4& color, bool wireframe = false) = 0;

        virtual void DrawSphere(const Vec3& center, float radius,
                                const Vec4& color, int32 segments = 16) = 0;

        virtual void DrawBox(const Vec3& center, const Vec3& halfExtents,
                             const Vec4& color, bool wireframe = true) = 0;

        virtual void DrawCapsule(const Vec3& center, float radius, float height,
                                 const Vec4& color, bool wireframe = true) = 0;

        virtual void DrawCylinder(const Vec3& center, float radius, float height,
                                  const Vec4& color, bool wireframe = true) = 0;

        // ── 文本 ──
        virtual void DrawText3D(const Vec3& position, const char* text,
                                const Vec4& color) = 0;

        // ── 坐标系 ──
        // 绘制 RGB 坐标轴（R=X, G=Y, B=Z）
        virtual void DrawAxes(const Mat4& transform, float length = 0.5f) = 0;

        // ── 清理 ──
        /** 每帧开始前调用 */
        virtual void Clear() = 0;
        /** 每帧结束后提交绘制（触发实际渲染） */
        virtual void Flush() = 0;
    };

} // namespace Engine
