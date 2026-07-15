/**
 * @file JoltDebugRenderer.cpp
 * @brief Jolt Physics Debug Renderer 实现 — v5.0 TLS 无锁版 (适配 Jolt v5.5.0)
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

// 在 Jolt v5.5.0 中，Vec3 到 Float3 的转换通过 ToFloat3() 方法完成
static JPH::Float3 ToFloat3(const JPH::Vec3& v) {
    JPH::Float3 result;
    result.x = v.GetX();
    result.y = v.GetY();
    result.z = v.GetZ();
    return result;
}

void JoltDebugRenderer::DrawLine(JPH::Vec3Arg inFrom, JPH::Vec3Arg inTo, JPH::ColorArg inColor) {
    if (!m_Draw) return;
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
        // v5.5.0: Triangle::mV 通过 Float3 存储，需要转换
        tri.mV[0] = ToFloat3(inVertices[inIndices[i]].mPosition);
        tri.mV[1] = ToFloat3(inVertices[inIndices[i + 1]].mPosition);
        tri.mV[2] = ToFloat3(inVertices[inIndices[i + 2]].mPosition);
        // v5.5.0: Triangle::mColor 是 Color 类型，Vertex::mColor 也是 Color
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
        // v5.5.0: Triangle::mV 是 Float3[3]，需要先转 Vec3
        JPH::Vec3 v0(tri.mV[0].x, tri.mV[0].y, tri.mV[0].z);
        JPH::Vec3 v1(tri.mV[1].x, tri.mV[1].y, tri.mV[1].z);
        JPH::Vec3 v2(tri.mV[2].x, tri.mV[2].y, tri.mV[2].z);
        // v5.5.0: RMat44 * Vec3 通过乘法运算符重载完成
        // 注意：RMat44 在 v5.5.0 中乘以 Vec3 返回 Vec3
        JPH::Vec3 wv0 = JPH::Vec3(inModelMatrix * JPH::RVec3(v0));
        JPH::Vec3 wv1 = JPH::Vec3(inModelMatrix * JPH::RVec3(v1));
        JPH::Vec3 wv2 = JPH::Vec3(inModelMatrix * JPH::RVec3(v2));
        // v5.5.0: Color 没有 IsValid() 方法，直接使用
        Vec4 color = ToVec4(inModelColor);
        buf.lines.push_back({ToVec3(wv0), ToVec3(wv1), color});
        buf.lines.push_back({ToVec3(wv1), ToVec3(wv2), color});
        buf.lines.push_back({ToVec3(wv2), ToVec3(wv0), color});
    }
}

void JoltDebugRenderer::DrawText3D(JPH::Vec3Arg inPosition,
                                    const std::string_view& inString,
                                    JPH::ColorArg inColor, float)
{
    if (!m_Draw) return;
    // v5.5.0: std::string_view 可用
    std::string text(inString);
    m_Draw->DrawText3D(ToVec3(inPosition), text.c_str(), ToVec4(inColor));
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