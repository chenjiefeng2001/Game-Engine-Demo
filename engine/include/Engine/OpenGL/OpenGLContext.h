#pragma once
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/RHI/AntiAliasingTypes.h"
#include <glad/gl.h>
#include <vector>
#include <cstdint>
#include <memory>

struct GLFWwindow;

namespace Engine {

    class OpenGLAntiAliasing;

    class OpenGLContext : public IRenderContext {
    public:
        OpenGLContext(GLFWwindow* windowHandle, GladGLContext& sharedGL);
        virtual ~OpenGLContext() override;

        virtual void Init() override;
        virtual void ClearColor(float r, float g, float b, float a) override;
        virtual void SwapBuffers() override;
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& va) override;
        virtual void OnResize(int width, int height) override;

        GladGLContext& GetGL() { return m_GL; }

        // ── 抗锯齿 ──
        OpenGLAntiAliasing* GetAntiAliasing() { return m_AntiAliasing.get(); }
        void SetAntiAliasingConfig(const AntiAliasingConfig& config);
        const AntiAliasingConfig& GetAntiAliasingConfig() const;

        // ── 截屏 ──
        virtual bool CaptureFrameBuffer(int32& outWidth, int32& outHeight,
                                        std::vector<uint8_t>& outPixels) override;

        // ── 统计 ──
        virtual uint32 GetAndResetDrawCallCount() override
        {
            uint32 count = m_DrawCallCount;
            m_DrawCallCount = 0;
            return count;
        }

    private:
        friend class PerformanceWindow;
        GLFWwindow* m_WindowHandle;
        GladGLContext& m_GL;
        uint32 m_DrawCallCount = 0;

        std::unique_ptr<OpenGLAntiAliasing> m_AntiAliasing;
        bool m_AntiAliasingInitialized = false;
    };
}