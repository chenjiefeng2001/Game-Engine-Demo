/**
 * @file JoltDebugRenderer.cpp
 * @brief Jolt Physics Debug Renderer 实现 — v5.0 TLS 无锁版 (适配 Jolt v5.5.0)
 */

#include "Engine/Jolt/JoltDebugRenderer.h"

#ifdef JPH_DEBUG_RENDERER
#include <cstring>
#include <algorithm>

namespace Engine {

JoltDebugRenderer::JoltDebugRenderer(IPhysicsDebugDraw3D* draw)
    : m_Draw(draw)
{
    Initialize();
}

JoltDebugRenderer::~JoltDebugRenderer() = default;

uint32 JoltDebugRenderer::GetThreadIndex() const {
    static std::atomic<uint32_t> s_NextIndex{0};
    thread_local uint32_t tls_Index = s_NextIndex.fetch_add(1, std::memory_order_relaxed) % MAX_THREADS;
    return tls_Index;
}

Vec4 JoltDebugRenderer::ToVec4(JPH::ColorArg color) {
    JPH::Color c = color;
    return Vec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

Vec3 JoltDebugRenderer::ToVec3(JPH::Vec3Arg v) {
    return Vec3(v.GetX(), v.GetY(), v.GetZ());
}

Vec3 JoltDebugRenderer::ToVec3FromRVec(JPH::RVec3Arg v) {
    return Vec3(float(v.GetX()), float(v.GetY()), float(v.GetZ()));
}

void JoltDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) {
    if (!m_Draw) return;
    uint32 idx = GetThreadIndex();
    PerThreadBuffer& buf = m_ThreadBuffers[idx];
    buf.lines.push_back({ToVec3FromRVec(inFrom), ToVec3FromRVec(inTo), ToVec4(inColor)});
}

void JoltDebugRenderer::DrawTriangle(JPH::RVec3Arg inV0, JPH::RVec3Arg inV1, JPH::RVec3Arg inV2,
                                      JPH::ColorArg inColor, JPH::DebugRenderer::ECastShadow) {
    if (!m_Draw) return;
    uint32 idx = GetThreadIndex();
    PerThreadBuffer& buf = m_ThreadBuffers[idx];
    Vec4 color = ToVec4(inColor);
    Vec3 v0 = ToVec3FromRVec(inV0), v1 = ToVec3FromRVec(inV1), v2 = ToVec3FromRVec(inV2);
    buf.lines.push_back({v0, v1, color});
    buf.lines.push_back({v1, v2, color});
    buf.lines.push_back({v2, v0, color});
}

JPH::DebugRenderer::Batch JoltDebugRenderer::CreateTriangleBatch(
    const Triangle* inTriangles, int inTriangleCount)
{
    (void)inTriangles;
    (void)inTriangleCount;
    return Batch();
}

JPH::DebugRenderer::Batch JoltDebugRenderer::CreateTriangleBatch(
    const Vertex* inVertices, int inVertexCount,
    const uint32* inIndices, int inIndexCount)
{
    (void)inVertices;
    (void)inVertexCount;
    (void)inIndices;
    (void)inIndexCount;
    return Batch();
}

void JoltDebugRenderer::DrawGeometry(JPH::RMat44Arg inModelMatrix,
                                      const JPH::AABox&, float,
                                      JPH::ColorArg inModelColor,
                                      const JPH::DebugRenderer::GeometryRef& inGeometry,
                                      JPH::DebugRenderer::ECullMode, JPH::DebugRenderer::ECastShadow, JPH::DebugRenderer::EDrawMode)
{
    if (!m_Draw || !inGeometry) return;
    (void)inModelMatrix;
    (void)inModelColor;
}

void JoltDebugRenderer::DrawText3D(JPH::RVec3Arg inPosition,
                                    const std::string_view& inString,
                                    JPH::ColorArg inColor, float)
{
    if (!m_Draw) return;
    std::string text(inString);
    m_Draw->DrawText3D(ToVec3FromRVec(inPosition), text.c_str(), ToVec4(inColor));
}

void JoltDebugRenderer::Clear() {
    for (auto& buf : m_ThreadBuffers) {
        buf.lines.clear();
    }
}

void JoltDebugRenderer::Flush() {
    if (!m_Draw) return;
    for (const auto& buf : m_ThreadBuffers) {
        for (const auto& line : buf.lines) {
            m_Draw->DrawLine(line.from, line.to, line.color);
        }
    }
}

} // namespace Engine
#endif // JPH_DEBUG_RENDERER