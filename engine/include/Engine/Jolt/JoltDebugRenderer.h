#pragma once

/**
 * @file JoltDebugRenderer.h
 * @brief Jolt Physics Debug Renderer — 将 Jolt 调试绘制转发到 IPhysicsDebugDraw3D
 *
 * v4.0: 继承 JPH::DebugRenderer，将 Jolt 输出的三角形/线段转发到引擎的
 * IPhysicsDebugDraw3D 接口。
 *
 * 线程安全：Jolt 的 DebugRenderer 由多个 Worker 线程调用，
 * 内部使用 std::mutex 保护顶点缓冲区。
 */

#include <Jolt/Renderer/DebugRenderer.h>
#include "Engine/Core/Physics/IPhysicsDebugDraw3D.h"
#include <vector>
#include <mutex>

namespace Engine {

class JoltDebugRenderer final : public JPH::DebugRenderer {
public:
    JoltDebugRenderer(IPhysicsDebugDraw3D* draw);
    ~JoltDebugRenderer() override;

    // ── JPH::DebugRenderer 纯虚接口 ──
    void DrawLine(JPH::Vec3Arg inFrom, JPH::Vec3Arg inTo, JPH::ColorArg inColor) override;

    void DrawTriangle(JPH::Vec3Arg inV0, JPH::Vec3Arg inV1, JPH::Vec3Arg inV2,
                      JPH::ColorArg inColor, ECullMode inCullMode) override;

    struct BatchImpl : public Batch {
        std::vector<JPH::DebugRenderer::Triangle> triangles;
    };

    Batch* CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) override;

    Batch* CreateTriangleBatch(const Vertex* inVertices, int inVertexCount,
                               const uint32* inIndices, int inIndexCount) override;

    void DrawGeometry(JPH::RMat44Arg inModelMatrix, const JPH::AABox& inWorldSpaceBounds,
                      float inLODScaleSq, JPH::ColorArg inModelColor,
                      const BatchRef& inGeometry, ECullMode inCullMode,
                      ECastShadow inCastShadow, EDrawMode inDrawMode) override;

    void DrawText3D(JPH::Vec3Arg inPosition, const std::string_view& inString,
                    JPH::ColorArg inColor, float inHeight) override;

    // ── 每帧生命周期 ──
    void Clear();
    void Flush();

private:
    static Vec4 ToVec4(JPH::ColorArg color);
    static Vec3 ToVec3(JPH::Vec3Arg v);

    IPhysicsDebugDraw3D* m_Draw;
    std::mutex m_Mutex;
};

} // namespace Engine