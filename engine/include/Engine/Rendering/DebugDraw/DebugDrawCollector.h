#pragma once

/**
 * @file DebugDrawCollector.h
 * @brief T-5 Generic DebugDraw — CPU collector
 *
 * Collector 只负责参数校验、图元展开、帧生命周期、预算和统计；
 * 所有 GPU 操作都通过 IRenderer 抽象完成。
 */

#include "Engine/Rendering/DebugDraw/DebugDrawTypes.h"
#include "Engine/Rendering/DebugDraw/IDebugDrawRenderer.h"

#include <thread>
#include <vector>

namespace Engine {
namespace Rendering {
namespace DebugDraw {

class Collector {
public:
    Collector() = default;
    ~Collector() = default;

    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;

    /** 绑定 Renderer 并记录 Owner Thread；不调用任何后端接口。 */
    Status Initialize(IRenderer* renderer);

    /** 清空收集状态；不负责关闭 Renderer。 */
    Status Shutdown();

    Status BeginFrame(const View& view, float32 deltaTimeSeconds);

    Status Line(const Vec3& from, const Vec3& to, const Vec4& color,
                float32 durationSeconds = 0.0f, bool depthTest = true);

    Status WireBox(const Vec3& center, const Vec3& halfExtents, const Quat& rotation,
                   const Vec4& color, float32 durationSeconds = 0.0f,
                   bool depthTest = true);

    Status WireSphere(const Vec3& center, float32 radius, const Vec4& color,
                      float32 durationSeconds = 0.0f, bool depthTest = true);

    Status Axis(const Vec3& origin, const Quat& rotation, float32 length,
                float32 durationSeconds = 0.0f, bool depthTest = true);

    /** corners = [nearBottomLeft, nearBottomRight, nearTopRight, nearTopLeft,
     *             farBottomLeft, farBottomRight, farTopRight, farTopLeft]。 */
    Status Frustum(const Vec3* corners, const Vec4& color,
                   float32 durationSeconds = 0.0f, bool depthTest = true);

    /** 结束帧：提交 depth-on / X-Ray 批次并调用 Renderer::Render。 */
    Status EndFrame();

    const Stats& GetStats() const { return m_Stats; }
    bool IsInitialized() const { return m_Initialized; }
    bool IsFrameActive() const { return m_InFrame; }

private:
    struct Command {
        std::vector<Vertex> vertices;
        float32 remainingSeconds = 0.0f;
        bool depthTest = true;
    };

    Status CheckCollectState() const;
    Status Append(std::vector<Vertex>&& vertices, float32 durationSeconds,
                  bool depthTest);
    void ClearAll();
    void AgePersistent(float32 deltaTimeSeconds);

    IRenderer* m_Renderer = nullptr;
    std::thread::id m_OwnerThread;

    bool m_Initialized = false;
    bool m_InFrame = false;
    bool m_BudgetWarningIssued = false;

    View m_View;
    float32 m_DeltaTimeSeconds = 0.0f;

    std::vector<Command> m_Persistent;
    std::vector<Command> m_Current;
    uint32 m_PersistentVertices = 0;
    uint32 m_CurrentVertices = 0;

    Stats m_Stats;
};

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
