#pragma once

#include "Engine/Rendering/DebugDraw/DebugDrawTypes.h"

namespace Engine {
namespace Rendering {
namespace DebugDraw {

/**
 * @brief DebugDraw 后端抽象。
 *
 * 实现负责后端初始化、批次提交和关闭。Collector 只持有此抽象，
 * 不包含任何 GL 调用；公共调用方不直接操作具体 GL Renderer。
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /** 后端初始化；必须返回明确状态。 */
    virtual Status Initialize() = 0;

    /** 幂等关闭；上下文不满足要求时必须返回失败且不得执行无效 GL 调用。 */
    virtual Status Shutdown() = 0;

    /** 提交 depth-on 与 X-Ray 两个顶点批次并完成实际绘制。 */
    virtual Status Render(const View& view, const FrameGeometry& geometry) = 0;

    /** 当前环境是否支持该后端；不支持的实现必须明确返回 false。 */
    virtual bool IsSupported() const = 0;
};

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
