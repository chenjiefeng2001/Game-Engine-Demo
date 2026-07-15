#pragma once

/**
 * @file JoltDebugRenderer.h
 * @brief Jolt Physics Debug Renderer — v5.0 TLS 无锁设计
 *
 * 注意：此头文件仅在 JPH_DEBUG_RENDERER 定义时有效，
 * 非 Debug 构建自动跳过 Jolt DebugRenderer 依赖。
 */

#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRenderer.h>
#endif

#include "Engine/Core/Physics/IPhysicsDebugDraw3D.h"
#include <vector>
#include <array>

namespace Engine {

#ifdef JPH_DEBUG_RENDERER
class JoltDebugRenderer final : public JPH::DebugRenderer {
public:
    static constexpr uint32 MAX_THREADS = 16;

    JoltDebugRenderer(IPhysicsDebugDraw3D* draw);
    ~JoltDebugRenderer() override;

    void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
    void DrawTriangle(JPH::RVec3Arg inV0, JPH::RVec3Arg inV1, JPH::RVec3Arg inV2,
                      JPH::ColorArg inColor, JPH::DebugRenderer::ECastShadow inCastShadow = JPH::DebugRenderer::ECastShadow::Off) override;
    JPH::DebugRenderer::Batch CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) override;
    JPH::DebugRenderer::Batch CreateTriangleBatch(const Vertex* inVertices, int inVertexCount,
                                                   const uint32* inIndices, int inIndexCount) override;
    void DrawGeometry(JPH::RMat44Arg inModelMatrix, const JPH::AABox& inWorldSpaceBounds,
                      float inLODScaleSq, JPH::ColorArg inModelColor,
                      const JPH::DebugRenderer::GeometryRef& inGeometry,
                      JPH::DebugRenderer::ECullMode inCullMode = JPH::DebugRenderer::ECullMode::CullBackFace,
                      JPH::DebugRenderer::ECastShadow inCastShadow = JPH::DebugRenderer::ECastShadow::On,
                      JPH::DebugRenderer::EDrawMode inDrawMode = JPH::DebugRenderer::EDrawMode::Solid) override;
    void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString,
                    JPH::ColorArg inColor, float inHeight) override;
    void Clear();
    void Flush();

private:
    struct Line { Vec3 from, to; Vec4 color; };
    struct PerThreadBuffer { std::vector<Line> lines; };
    static Vec4 ToVec4(JPH::ColorArg color);
    static Vec3 ToVec3(JPH::Vec3Arg v);
    static Vec3 ToVec3FromRVec(JPH::RVec3Arg v);
    uint32 GetThreadIndex() const;
    IPhysicsDebugDraw3D* m_Draw;
    std::array<PerThreadBuffer, MAX_THREADS> m_ThreadBuffers;
};
#else
// JPH_DEBUG_RENDERER 未定义时的桩类
class JoltDebugRenderer {
public:
    JoltDebugRenderer(IPhysicsDebugDraw3D*) {}
    void Clear() {}
    void Flush() {}
};
#endif

} // namespace Engine