/**
 * @file JoltDebugRenderer.cpp
 * @brief Jolt Physics Debug Renderer 实现 — v5.0 TLS 无锁版
 *
 * v5.0 设计：
 *   - 16 个 PerThreadBuffer（每个 Worker 线程一个）
 *   - GetThreadIndex() 通过 std::this_thread::get_id() 哈希映射
 *   - DrawLine/DrawTriangle 写入各自线程的 buffer，完全无锁
 *   - Flush() 汇总所有线程数据到 IPhysicsDebugDraw3D
 */

#include "Engine/Jolt/JoltDebugRenderer.h"
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
    // 使用 thread_id 哈希后取模，确保每个线程有稳定的槽位
    static thread_local uint32 s_CachedIndex = 0xFFFFFFFF;
    if (s_CachedIndex == 0xFFFFFFFF) {
        auto id = std::this_thread::get_id();
        std::hash<std::thread::id> hasher;
        s_CachedIndex = static_cast<uint32>(hasher(id) % MAX_THREADS);
    }
    return s_CachedIndex;
}

Vec4 JoltDebugRenderer::ToVec4(JPH::ColorArg color) {
    JPH::Color c = color;
    return Vec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

Vec3 JoltDebugRenderer::ToVec3(JPH::Vec3Arg v) {
    return Vec3(v.GetX(), v.GetY(), v.GetZ());
}

void JoltDebugRenderer::DrawLine(JPH::Vec3Arg inFrom, JPH::Vec3Arg inTo, JPH::ColorArg inColor) {
    if (!m_Draw) return;
    // TLS: 无锁写入
    uint32 idx = GetThreadIndex();
    PerThreadBuffer& buf = m_ThreadBuffers[idx];
    buf.lines.push_back({ToVec3(inFrom), ToVec3(inTo), ToVec4(inColor)});
}

void JoltDebugRenderer::DrawTriangle(JPH::Vec3Arg inV0, JPH::Vec3Arg inV1, JPH::Vec3Arg inV2,
                                      JPH::ColorArg inColor, ECullMode) {
    if (!m_Draw) return;
    uint32 idx = GetThreadIndex();
    PerThreadBuffer& buf = m_ThreadBuffers[idx];
    Vec4 color = ToVec4(inColor);
    Vec3 v0 = ToVec3(inV0), v1 = ToVec3(inV1), v2 = ToVec3(inV2);
    buf.lines.push_back({v0, v1, color});
    buf.lines.push_back({v1, v2, color});
    buf.lines.push_back({v2, v0, color});
}

JPH::DebugRenderer::Batch* JoltDebugRenderer::CreateTriangleBatch(
    const Triangle* inTriangles, int inTriangleCount)
{
    if (inTriangleCount == 0) return nullptr;
    BatchImpl* batch = new BatchImpl();
    batch->triangles.assign(inTriangles, inTriangles + inTriangleCount);
    return batch;
}

JPH::DebugRenderer::Batch* JoltDebugRenderer::CreateTriangleBatch(
    const Vertex* inVertices, int inVertexCount,
    const uint32* inIndices, int inIndexCount)
{
    if (inIndexCount == 0) return nullptr;
    BatchImpl* batch = new BatchImpl();
    batch->triangles.reserve(inIndexCount / 3);
    for (int i = 0; i + 2 < inIndexCount; i += 3) {
        JPH::DebugRenderer::Triangle tri;
        tri.mV[0] = inVertices[inIndices[i]].mPosition;
        tri.mV[1] = inVertices[inIndices[i + 1]].mPosition;
        tri.mV[2] = inVertices[inIndices[i + 2]].mPosition;
        tri.mColor = inVertices[inIndices[i]].mColor;
        batch->triangles.push_back(tri);
    }
    return batch;
}

void JoltDebugRenderer::DrawGeometry(JPH::RMat44Arg inModelMatrix,
                                      const JPH::AABox&, float,
                                      JPH::ColorArg inModelColor,
                                      const BatchRef& inGeometry, ECullMode,
                                      ECastShadow, EDrawMode)
{
    if (!m_Draw || !inGeometry) return;
    uint32 idx = GetThreadIndex();
    PerThreadBuffer& buf = m_ThreadBuffers[idx];
    const BatchImpl* batch = static_cast<const BatchImpl*>(inGeometry.GetPtr());

    for (const auto& tri : batch->triangles) {
        JPH::Vec3 v0 = inModelMatrix * tri.mV[0];
        JPH::Vec3 v1 = inModelMatrix * tri.mV[1];
        JPH::Vec3 v2 = inModelMatrix * tri.mV[2];
        Vec4 color = inModelColor.IsValid() ? ToVec4(inModelColor) : ToVec4(tri.mColor);
        buf.lines.push_back({ToVec3(v0), ToVec3(v1), color});
        buf.lines.push_back({ToVec3(v1), ToVec3(v2), color});
        buf.lines.push_back({ToVec3(v2), ToVec3(v0), color});
    }
}

void JoltDebugRenderer::DrawText3D(JPH::Vec3Arg inPosition,
                                    const std::string_view& inString,
                                    JPH::ColorArg inColor, float)
{
    if (!m_Draw) return;
    std::string text(inString);
    m_Draw->DrawText3D(ToVec3(inPosition), text.c_str(), ToVec4(inColor));
}

void JoltDebugRenderer::Clear() {
    // 清空所有线程的缓冲区（主线程调用，安全）
    for (auto& buf : m_ThreadBuffers) {
        buf.lines.clear();
    }
}

void JoltDebugRenderer::Flush() {
    if (!m_Draw) return;

    // 汇总所有线程的数据到 IPhysicsDebugDraw3D
    for (const auto& buf : m_ThreadBuffers) {
        for (const auto& line : buf.lines) {
            m_Draw->DrawLine(line.from, line.to, line.color);
        }
    }
}

} // namespace Engine