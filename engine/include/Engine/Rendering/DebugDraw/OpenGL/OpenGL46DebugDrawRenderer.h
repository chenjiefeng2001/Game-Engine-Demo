#pragma once

#include "Engine/Rendering/DebugDraw/DebugDrawTypes.h"
#include "Engine/Rendering/DebugDraw/IDebugDrawRenderer.h"

#include <thread>

struct GladGLContext;

namespace Engine {
namespace Rendering {
namespace DebugDraw {

/**
 * @brief T-5 OpenGL 4.6 DebugDraw 后端。
 *
 * 独立于 IPrimitiveBatch / RenderGraph；只负责 GL 资源、批次提交和状态隔离。
 * 所有生命周期调用都必须在 Owner Thread 且 current GL context 下执行。
 */
class OpenGL46DebugDrawRenderer final : public IRenderer {
public:
    explicit OpenGL46DebugDrawRenderer(GladGLContext& gl);
    ~OpenGL46DebugDrawRenderer() override;

    OpenGL46DebugDrawRenderer(const OpenGL46DebugDrawRenderer&) = delete;
    OpenGL46DebugDrawRenderer& operator=(const OpenGL46DebugDrawRenderer&) = delete;

    Status Initialize() override;
    Status Shutdown() override;
    Status Render(const View& view, const FrameGeometry& geometry) override;
    bool IsSupported() const override;

private:
    Status CheckContextSupport() const;
    Status CompileProgram();
    Status CreateBuffers();
    void DestroyResources();

    GladGLContext& m_GL;
    std::thread::id m_OwnerThread;
    bool m_Initialized = false;

    uint32 m_Program = 0;
    uint32 m_VAO = 0;
    uint32 m_VBO = 0;
    int32 m_ViewProjectionLocation = -1;
};

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
