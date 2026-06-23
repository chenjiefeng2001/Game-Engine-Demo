#pragma once
#include <string>
#include <memory>
#include <functional>
#include "Engine/Types.h"

namespace Engine {
	class IRenderContext; // 前向声明
	enum class EventType {
		WindowResize, KeyPress, KeyRelease,
		MouseMove, MouseClick, WindowClose, MouseScroll,
		WindowFocusGained, WindowFocusLost,
	};
	struct Event {
		EventType type;
		union {
			struct { int32 width, height; } resize;
			struct { int32 key, scancode, action, mods; } key;
			struct { float64 x, y; } mouseMove;
			struct { int32 button, action, mods; } mouseButton;
			struct { float64 xOffset, yOffset; } mouseScroll;
		};
	};

	class IWindow {
	public:
		//Event Call
		using EventCallbackFn = std::function<void(Event&)>;

		//Deconstructor Func
		virtual ~IWindow() = default;

		virtual void OnUpdate() = 0;
		virtual void PollEvents() = 0;
		/// @brief 阻塞等待下一个事件（或超时后返回）
		/// @param timeoutSec 超时秒数，≤0 表示无限等待
		virtual void WaitEvents(double timeoutSec = 0.0) = 0;
		virtual bool ShouldClose() const = 0;
		/// @brief 设置窗口关闭标志（用于 Exit 菜单项等）
		virtual void SetShouldClose(bool close) = 0;

		// ── 窗口状态查询 ──
		/// @brief 窗口是否获得焦点（激活状态）
		virtual bool IsActive() const = 0;
		/// @brief 窗口是否最小化
		virtual bool IsMinimized() const = 0;
		/// @brief 窗口是否正在被用户拖动调整大小
		virtual bool IsResizing() const = 0;

		virtual IRenderContext* GetContext() = 0;
		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void* GetNativeHandle() const = 0;
	};
}