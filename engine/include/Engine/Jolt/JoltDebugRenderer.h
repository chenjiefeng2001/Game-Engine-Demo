#pragma once

/**
 * @file JoltDebugRenderer.h
 * @brief Jolt Physics Debug Renderer — v5.0 TLS 无锁设计
 *
 * Jolt 在多个 Worker 线程并发调用 DrawLine/DrawTriangle。
 * v4.0 使用 std::mutex 保护单个缓冲区 → 锁竞争极其严重。
 * v5.0 改用 Thread-Local Storage (TLS)：
 *   - 每个线程拥有独立的 Line 缓冲区
 *   - 写入完全无锁，零竞争
 *   - Flush() 阶段在主线程汇总所有线程的数据并提交渲染
 */

#include <Jolt/Renderer/DebugRenderer.h>
#include "Engine/Core/Physics/IPhysicsDebugDraw3D.h"
#include "Engine/Core/JobSystem.h"
#include <vector>
#include <array>
#include <thread>

namespace Engine {

class JoltDebugRenderer final : public JPH::DebugRenderer {
public:
    static constexpr uint32 MAX_THREADS = 16;

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

    // ── 每帧生命周期（主线程调用）──
    void Clear();
    void Flush();

private:
    struct Line { Vec3 from, to; Vec4 color; };
    struct PerThreadBuffer {
        std::vector<Line> lines;
    };

    static Vec4 ToVec4(JPH::ColorArg color);
    static Vec3 ToVec3(JPH::Vec3Arg v);
    uint32 GetThreadIndex() const;

    IPhysicsDebugDraw3D* m_Draw;
    std::array<PerThreadBuffer, MAX_THREADS> m_ThreadBuffers;
};

} // namespace Engine