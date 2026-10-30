#pragma once

#include "Engine/OpenGL/OpenGLFramebuffer.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"
#include "Engine/Platform/GlfwLifetime.h"

#include <memory>
#include <string>

namespace Engine {
namespace Rendering {
namespace DebugDraw {
namespace Test {

/**
 * @brief 最小隐藏 GL 4.6 上下文 + FBO 夹具。
 *
 * 与 test_pick 使用同一套现有设施（OpenGLGraphicsFactory / OpenGLFramebuffer），
 * 不复制 FBO 实现，也不修改 test_pick。
 */
class DebugDrawGLFixture {
public:
    DebugDrawGLFixture() = default;
    ~DebugDrawGLFixture() { Teardown(); }

    DebugDrawGLFixture(const DebugDrawGLFixture&) = delete;
    DebugDrawGLFixture& operator=(const DebugDrawGLFixture&) = delete;

    bool Setup() {
        if (!::Engine::RegisterGlfwOwnerThread()) {
            m_FailStep = "RegisterGlfwOwnerThread";
            return false;
        }

        m_Factory = std::make_unique<::Engine::OpenGLGraphicsFactory>(true);
        if (!m_Factory->InitializePersistentHiddenContext(64, 64)) {
            m_FailStep = "InitializePersistentHiddenContext: " +
                         m_Factory->GetLastError();
            return false;
        }
        if (!m_Factory->MakePersistentContextCurrent()) {
            m_FailStep = "MakePersistentContextCurrent: " +
                         m_Factory->GetLastError();
            return false;
        }

        m_Fbo = std::make_unique<::Engine::OpenGLFramebuffer>(
            m_Factory->GetGLContext());
        if (!m_Fbo->Resize(kWidth, kHeight)) {
            m_FailStep = "OpenGLFramebuffer::Resize";
            return false;
        }
        if (!m_Fbo->IsValid()) {
            m_FailStep = "OpenGLFramebuffer::IsValid";
            return false;
        }
        m_FailStep.clear();
        return true;
    }

    void Teardown() {
        m_Fbo.reset();
        if (m_Factory) {
            (void)m_Factory->UncurrentPersistentContext();
            m_Factory.reset();
        }
    }

    ::Engine::OpenGLGraphicsFactory& Factory() { return *m_Factory; }
    ::Engine::OpenGLFramebuffer& Fbo() { return *m_Fbo; }
    GladGLContext& GL() { return m_Factory->GetGLContext(); }
    const std::string& FailStep() const { return m_FailStep; }

    static constexpr int kWidth = 128;
    static constexpr int kHeight = 128;

private:
    std::unique_ptr<::Engine::OpenGLGraphicsFactory> m_Factory;
    std::unique_ptr<::Engine::OpenGLFramebuffer> m_Fbo;
    std::string m_FailStep;
};

} // namespace Test
} // namespace DebugDraw
} // namespace Rendering
} // namespace Engine
