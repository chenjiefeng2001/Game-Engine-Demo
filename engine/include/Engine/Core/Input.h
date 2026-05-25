#pragma once
#include <memory>
#include "Engine/Types.h"
namespace Engine {


	enum class KeyCode : uint16 {
		A = 0, B, C, D, E, F, G, H, I, J, K, L, M,
		N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

		Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

		Escape, Enter, Tab, Space, Backspace,
		LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
		Up, Down, Left, Right,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

		GraveAccent, Minus, Equal, LeftBracket, RightBracket,
		Semicolon, Apostrophe, Comma, Period, Slash, Backslash,

		COUNT
	};

	enum class MouseCode : uint8 {
		ButtonLeft = 0,
		ButtonRight,
		ButtonMiddle,
		Button4,
		Button5,
		Button6,
		Button7,
		Button8,
		COUNT

	};

	class IInput {
	public:
		virtual ~IInput() = default;


		virtual bool IsKeyDown(KeyCode key) const = 0;
		virtual bool IsKeyPressed(KeyCode key) const = 0;
		virtual bool IsKeyReleased(KeyCode key) const = 0;

		virtual bool IsMouseButtonDown(MouseCode button) const = 0;
		virtual bool IsMouseButtonPressed(MouseCode button) const = 0;
		virtual bool IsMouseButtonReleased(MouseCode button) const = 0;

		virtual float GetMouseX() const = 0;
		virtual float GetMouseY() const = 0;
		virtual float GetMouseDeltaX() const = 0;
		virtual float GetMouseDeltaY() const = 0;
		virtual float GetScrollDelta() const = 0;

		virtual void OnUpdate() = 0;

		virtual void OnKeyEvent(int nativeKey, int action) = 0;
		virtual void OnMouseButtonEvent(int nativeButton, int action) = 0;
		virtual void OnMouseMove(float x, float y) = 0;
		virtual void OnScroll(float xOffset, float yOffset) = 0;
	};


	class Input {
	public:
		static void Init(std::unique_ptr<IInput> instance);
		static void Shutdown();
		static IInput* Get() { return s_Instance; }

		// ── 输入抢占控制（Dear ImGui 激活时阻止游戏输入） ──
		static void SetBlockInput(bool blockMouse, bool blockKeyboard)
		{
			s_BlockMouse = blockMouse;
			s_BlockKeyboard = blockKeyboard;
		}
		static bool IsMouseBlocked()    { return s_BlockMouse; }
		static bool IsKeyboardBlocked() { return s_BlockKeyboard; }

		// ── 键盘查询（UI 激活时返回 false） ──
		static bool IsKeyDown(KeyCode key) {
			return s_Instance && !s_BlockKeyboard && s_Instance->IsKeyDown(key);
		}
		static bool IsKeyPressed(KeyCode key) {
			return s_Instance && !s_BlockKeyboard && s_Instance->IsKeyPressed(key);
		}
		static bool IsKeyReleased(KeyCode key) {
			return s_Instance && !s_BlockKeyboard && s_Instance->IsKeyReleased(key);
		}

		// ── 鼠标按钮查询（UI 激活时返回 false） ──
		static bool IsMouseButtonDown(MouseCode b) {
			return s_Instance && !s_BlockMouse && s_Instance->IsMouseButtonDown(b);
		}
		static bool IsMouseButtonPressed(MouseCode b) {
			return s_Instance && !s_BlockMouse && s_Instance->IsMouseButtonPressed(b);
		}
		static bool IsMouseButtonReleased(MouseCode b) {
			return s_Instance && !s_BlockMouse && s_Instance->IsMouseButtonReleased(b);
		}

		// ── 鼠标位置／增量查询（UI 激活时返回 0） ──
		static float GetMouseX() {
			return (!s_Instance || s_BlockMouse) ? 0.0f : s_Instance->GetMouseX();
		}
		static float GetMouseY() {
			return (!s_Instance || s_BlockMouse) ? 0.0f : s_Instance->GetMouseY();
		}
		static float GetMouseDeltaX() {
			return (!s_Instance || s_BlockMouse) ? 0.0f : s_Instance->GetMouseDeltaX();
		}
		static float GetMouseDeltaY() {
			return (!s_Instance || s_BlockMouse) ? 0.0f : s_Instance->GetMouseDeltaY();
		}
		static float GetScrollDelta() {
			return (!s_Instance || s_BlockMouse) ? 0.0f : s_Instance->GetScrollDelta();
		}

	private:
		static IInput* s_Instance;
		static std::unique_ptr<IInput> s_InstanceOwner;
		static bool s_BlockMouse;
		static bool s_BlockKeyboard;
	};

}