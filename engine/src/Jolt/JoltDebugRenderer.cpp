/**
 * @file JoltDebugRenderer.cpp
 * @brief Jolt Physics Debug Renderer 实现
 *
 * 将 Jolt 原生的调试绘制（三角形批处理、线段、文本）转发到
 * 引擎的 IPhysicsDebugDraw3D 接口。
 */

#include "Engine/Jolt/JoltDebugRenderer.h"

namespace Engine {

JoltDebugRenderer::JoltDebugRenderer(IPhysicsDebugDraw3D* draw)
    : m_Draw(draw)
{
    // Initialize base class with no actual backend
    Initialize();
}

JoltDebugRenderer::~JoltDebugRenderer() = default;

Vec4 JoltDebugRenderer::ToVec4(JPH::ColorArg color) {
    JPH::Color c = color;
    return Vec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

Vec3 JoltDebugRenderer::ToVec3(JPH::Vec3Arg v) {
    return Vec3(v.GetX(), v.GetY(), v.GetZ());
}

void JoltDebugRenderer::DrawLine(JPH::Vec3Arg inFrom, JPH::Vec3Arg inTo, JPH::ColorArg inColor) {
    if (!m_Draw) return;
    std::lock_guard<std::mutex> lock(m_Mutex);
    Vec4 color = ToVec4(inColor);
    m_Draw->DrawLine(ToVec3(inFrom), ToVec3(inTo), color);
}

void JoltDebugRenderer::DrawTriangle(JPH::Vec3Arg inV0, JPH::Vec3Arg inV1, JPH::Vec3Arg inV2,
                                      JPH::ColorArg inColor, ECullMode) {
    if (!m_Draw) return;
    std::lock_guard<std::mutex> lock(m_Mutex);
    Vec4 color = ToVec4(inColor);
    m_Draw->DrawLine(ToVec3(inV0), ToVec3(inV1), color);
    m_Draw->DrawLine(ToVec3(inV1), ToVec3(inV2), color);
    m_Draw->DrawLine(ToVec3(inV2), ToVec3(inV0), color);
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
                                      const JPH::AABox&,
                                      float,
                                      JPH::ColorArg inModelColor,
                                      const BatchRef& inGeometry, ECullMode,
                                      ECastShadow, EDrawMode)
{
    if (!m_Draw || !inGeometry) return;

    std::lock_guard<std::mutex> lock(m_Mutex);
    const BatchImpl* batch = static_cast<const BatchImpl*>(inGeometry.GetPtr());

    for (const auto& tri : batch->triangles) {
        JPH::Vec3 v0 = inModelMatrix * tri.mV[0];
        JPH::Vec3 v1 = inModelMatrix * tri.mV[1];
        JPH::Vec3 v2 = inModelMatrix * tri.mV[2];

        Vec4 color = inModelColor.IsValid() ? ToVec4(inModelColor) : ToVec4(tri.mColor);
        m_Draw->DrawLine(ToVec3(v0), ToVec3(v1), color);
        m_Draw->DrawLine(ToVec3(v1), ToVec3(v2), color);
        m_Draw->DrawLine(ToVec3(v2), ToVec3(v0), color);
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
    if (m_Draw) m_Draw->Clear();
}

void JoltDebugRenderer::Flush() {
    if (m_Draw) m_Draw->Flush();
}

} // namespace Engine