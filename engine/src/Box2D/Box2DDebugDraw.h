#pragma once

/**
 * @file Box2DDebugDraw.h
 * @brief Box2D 调试绘制桥接（Box2D 3.x）
 *
 * 私有头文件，仅在 Box2D 实现层使用。
 * 提供静态函数指针供 b2DebugDraw 结构体使用，转发到引擎抽象的 IPhysicsDebugDraw 接口。
 */

#include "Engine/Core/Physics/IPhysicsDebugDraw.h"
#include <box2d/box2d.h>

namespace Engine {

    class Box2DDebugDraw {
    public:
        /**
         * @brief 初始化 b2DebugDraw 结构体，绑定到目标绘制接口
         * @param target 引擎调试绘制抽象接口
         * @return 填充好的 b2DebugDraw 结构体（可传入 b2World_Draw）
         */
        static b2DebugDraw Create(IPhysicsDebugDraw& target);

    private:
        // ── 静态回调函数（供 b2DebugDraw 函数指针使用） ──

        static void DrawPolygonImpl(b2WorldTransform transform,
                                    const b2Vec2* vertices, int vertexCount,
                                    b2HexColor color, void* context);
        static void DrawSolidPolygonImpl(b2WorldTransform transform,
                                         const b2Vec2* vertices, int vertexCount,
                                         float radius, b2HexColor color,
                                         void* context);
        static void DrawCircleImpl(b2Vec2 center, float radius,
                                   b2HexColor color, void* context);
        static void DrawSolidCircleImpl(b2WorldTransform transform, b2Vec2 center,
                                        float radius, b2HexColor color, void* context);
        static void DrawSolidCapsuleImpl(b2Vec2 p1, b2Vec2 p2, float radius,
                                         b2HexColor color, void* context);
        static void DrawLineImpl(b2Vec2 p1, b2Vec2 p2,
                                 b2HexColor color, void* context);
        static void DrawTransformImpl(b2WorldTransform transform, void* context);
        static void DrawPointImpl(b2Vec2 p, float size,
                                  b2HexColor color, void* context);

        // ── 工具 ──
        static Vec2      FromB2(b2Vec2 v);
        static DebugColor FromB2Color(b2HexColor color);
    };

} // namespace Engine
