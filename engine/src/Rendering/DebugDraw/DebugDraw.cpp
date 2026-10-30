#include "Engine/Rendering/DebugDraw/DebugDraw.h"

#include "Engine/Core/Log.h"

namespace Engine {
namespace Rendering {
namespace DebugDraw {

DebugDraw::~DebugDraw() {
    (void)Shutdown();
}

Status DebugDraw::Initialize(std::unique_ptr<IRenderer> renderer) {
    if (m_Initialized || m_Renderer) return Status::AlreadyInitialized;
    if (!renderer) return Status::InvalidArgument;

    const Status rendererStatus = renderer->Initialize();
    if (rendererStatus != Status::Ok) {
        ENGINE_LOG_ERROR("DebugDraw", "renderer initialize failed: {}",
                         StatusToString(rendererStatus));
        return rendererStatus;
    }

    const Status collectorStatus = m_Collector.Initialize(renderer.get());
    if (collectorStatus != Status::Ok) {
        (void)renderer->Shutdown();
        ENGINE_LOG_ERROR("DebugDraw", "collector initialize failed: {}",
                         StatusToString(collectorStatus));
        return collectorStatus;
    }

    m_Renderer = std::move(renderer);
    m_Initialized = true;
    return Status::Ok;
}

Status DebugDraw::Shutdown() {
    if (!m_Renderer && !m_Collector.IsInitialized()) return Status::Ok;

    const Status collectorStatus = m_Collector.Shutdown();
    if (collectorStatus != Status::Ok) {
        ENGINE_LOG_ERROR("DebugDraw", "collector shutdown failed: {}",
                         StatusToString(collectorStatus));
        return collectorStatus;
    }

    if (!m_Renderer) {
        m_Initialized = false;
        return Status::Ok;
    }

    const Status rendererStatus = m_Renderer->Shutdown();
    if (rendererStatus != Status::Ok) {
        // 保留 renderer：上下文恢复后调用方仍可再次 Shutdown 重试。
        ENGINE_LOG_ERROR("DebugDraw", "renderer shutdown failed: {}",
                         StatusToString(rendererStatus));
        m_Initialized = false;
        return rendererStatus;
    }

    m_Renderer.reset();
    m_Initialized = false;
    return Status::Ok;
}

Status DebugDraw::BeginFrame(const View& view, float32 deltaTimeSeconds) {
    return m_Collector.BeginFrame(view, deltaTimeSeconds);
}

Status DebugDraw::EndFrame() {
    const Status status = m_Collector.EndFrame();
    if (status != Status::Ok) {
        ENGINE_LOG_ERROR("DebugDraw", "end frame failed: {}",
                         StatusToString(status));
    }
    return status;
}

} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
