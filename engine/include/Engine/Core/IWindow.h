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
		virtual bool ShouldClose() const = 0;
		virtual IRenderContext* GetContext() = 0;
		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void* GetNativeHandle() const = 0;
	};
}