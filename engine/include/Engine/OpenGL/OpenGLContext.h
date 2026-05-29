#pragma once
#include "Engine/Core/IRenderContext.h"
#include <glad/gl.h>
#include <vector>
#include <cstdint>

struct GLFWwindow;

namespace Engine {
    class OpenGLContext : public IRenderContext {
    public:
        OpenGLContext(GLFWwindow* windowHandle, GladGLContext& sharedGL);
        virtual ~OpenGLContext() override = default;

        virtual void Init() override;
        virtual void ClearColor(float r, float g, float b, float a) override;
        virtual void SwapBuffers() override;
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& va) override;
        virtual void OnResize(int width, int height) override;  // ← 新增

        GladGLContext& GetGL() { return m_GL; }

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
        friend class PerformanceWindow;  // 允许直接访问统计
        GLFWwindow* m_WindowHandle;
        GladGLContext& m_GL;
        uint32 m_DrawCallCount = 0;
    };
}