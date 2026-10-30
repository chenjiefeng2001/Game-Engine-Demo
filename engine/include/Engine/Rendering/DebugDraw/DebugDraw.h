#pragma once

/**
 * @file DebugDraw.h
 * @brief T-5 Generic DebugDraw — 对外统一服务入口
 *
 * 调用顺序：
 *   Initialize(renderer);
 *   BeginFrame(view, deltaTimeSeconds);
 *   Line/WireBox/WireSphere/Axis/Frustum(...);
 *   EndFrame();       // 提交批次并完成实际绘制
 *   Shutdown();
 */

#include "Engine/Rendering/DebugDraw/DebugDrawCollector.h"

#include <memory>

namespace Engine {
namespace Rendering {
namespace DebugDraw {

class DebugDraw {
public:
    DebugDraw() = default;
    ~DebugDraw();

    DebugDraw(const DebugDraw&) = delete;
    DebugDraw& operator=(const DebugDraw&) = delete;

    /** 接管 renderer；失败时不保存所有权。 */
    Status Initialize(std::unique_ptr<IRenderer> renderer);

    /** 幂等关闭；上下文不满足要求时返回失败并保留 renderer 以便重试。 */
    Status Shutdown();

    Status BeginFrame(const View& view, float32 deltaTimeSeconds);

    Status Line(const Vec3& from, const Vec3& to, const Vec4& color,
                float32 durationSeconds = 0.0f, bool depthTest = true) {
        return m_Collector.Line(from, to, color, durationSeconds, depthTest);
    }

    Status WireBox(const Vec3& center, const Vec3& halfExtents, const Quat& rotation,
                   const Vec4& color, float32 durationSeconds = 0.0f,
                   bool depthTest = true) {
        return m_Collector.WireBox(center, halfExtents, rotation, color,
                                   durationSeconds, depthTest);
    }

    Status WireSphere(const Vec3& center, float32 radius, const Vec4& color,
                      float32 durationSeconds = 0.0f, bool depthTest = true) {
        return m_Collector.WireSphere(center, radius, color,
                                      durationSeconds, depthTest);
    }

    Status Axis(const Vec3& origin, const Quat& rotation, float32 length,
                float32 durationSeconds = 0.0f, bool depthTest = true) {
        return m_Collector.Axis(origin, rotation, length,
                                durationSeconds, depthTest);
    }

    Status Frustum(const Vec3* corners, const Vec4& color,
                   float32 durationSeconds = 0.0f, bool depthTest = true) {
        return m_Collector.Frustum(corners, color, durationSeconds, depthTest);
    }

    Status EndFrame();

    const Stats& GetStats() const { return m_Collector.GetStats(); }
    bool IsInitialized() const { return m_Initialized; }

private:
    Collector m_Collector;
    std::unique_ptr<IRenderer> m_Renderer;
    bool m_Initialized = false;
};

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
