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

        // ── 深度预渲染 ──
        virtual void SetDepthMask(bool enable) override;
        virtual void SetColorMask(bool r, bool g, bool b, bool a) override;
        virtual void SetDepthFunc(uint32 func) override;

        /** 仅执行 resolve（AA FBO→默认帧缓冲），不 swap。
         *  用于在 ImGui/HUD 之前手动 resolve 场景。 */
        void ResolveToDefault();

    private:
        friend class PerformanceWindow;
        GLFWwindow* m_WindowHandle;
        GladGLContext& m_GL;
        uint32 m_DrawCallCount = 0;

        std::unique_ptr<OpenGLAntiAliasing> m_AntiAliasing;
        bool m_AntiAliasingInitialized = false;
    };
}