#pragma once
#include "Engine/Core/IWindow.h"
#include "Engine/Core/IRenderContext.h"

struct GLFWwindow;

namespace Engine {
	class GlfwWindow : public IWindow {
	public:
		GlfwWindow(GLFWwindow* nativeWindow,
			std::unique_ptr<IRenderContext> context);
		virtual ~GlfwWindow() override;

		// ---- IWindow 接口 ----
		virtual void OnUpdate() override;
		virtual void PollEvents() override;
		virtual void WaitEvents(double timeoutSec = 0.0) override;
		virtual bool ShouldClose() const override;
		virtual void SetShouldClose(bool close) override;

		// ── 窗口状态查询 ──
		virtual bool IsActive() const override;
		virtual bool IsMinimized() const override;
		virtual bool IsResizing() const override;

		virtual IRenderContext* GetContext() override { return m_Context.get(); }
		virtual void SetEventCallback(const EventCallbackFn& callback) override
		{
			m_EventCallback = callback;
		}

		// ---- GLFW 事件回调 ----
		void OnResize(int width, int height);
		void OnKey(int key, int scancode, int action, int mods);
		void OnClose();
		void OnFocus(int focused);
		void OnMouseMove(double x, double y);
		void OnMouseButton(int button, int action, int mods);
		void OnScroll(double xOffset, double yOffset);
		virtual void* GetNativeHandle() const override { return m_Window; }
	private:
		GLFWwindow* m_Window;
		std::unique_ptr<IRenderContext> m_Context;
		EventCallbackFn m_EventCallback;

		// ── 窗口状态跟踪 ──
		bool m_IsActive   = true;
		bool m_IsMinimized = false;
		bool m_IsResizing = false;
	};
}