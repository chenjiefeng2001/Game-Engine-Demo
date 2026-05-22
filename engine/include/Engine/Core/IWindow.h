#pragma once
#include <string>
#include <memory>
#include <functional>
#include <stdint.h>

namespace Engine {
	class IRenderContext; // 前向声明
	enum class EventType {
		WindowResize, KeyPress, KeyRelease,
		MouseMove, MouseClick, WindowClose,
	};
	struct Event {
		EventType type;
		union {
			struct { int width, height; } resize;
			struct { int key, scancode, action, mods; } key;
			struct { double x, y; } mouseMove;
			// ...
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
	};
}