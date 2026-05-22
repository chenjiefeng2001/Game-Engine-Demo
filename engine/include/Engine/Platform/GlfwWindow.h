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
		virtual bool ShouldClose() const override;
		virtual IRenderContext* GetContext() override { return m_Context.get(); }
		virtual void SetEventCallback(const EventCallbackFn& callback) override
		{
			m_EventCallback = callback;
		}

		// ---- GLFW 事件回调 ----
		void OnResize(int width, int height);
		void OnKey(int key, int scancode, int action, int mods);
		void OnClose();
		void OnMouseMove(double x, double y);
		void OnMouseButton(int button, int action, int mods);
		void OnScroll(double xOffset, double yOffset);
		virtual void* GetNativeHandle() const override { return m_Window; }
	private:
		GLFWwindow* m_Window;
		std::unique_ptr<IRenderContext> m_Context;
		EventCallbackFn m_EventCallback;
	};
}