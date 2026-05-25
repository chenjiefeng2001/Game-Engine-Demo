#include "Engine/Platform/GlfwInput.h"
#include <GLFW/glfw3.h>
#include <cstring>

namespace Engine {
	KeyCode GlfwInput::GLFWKeyToEngine(int nativeKey) {
		// GLFW key constants map directly to ASCII for letters/numbers
		if (nativeKey >= GLFW_KEY_A && nativeKey <= GLFW_KEY_Z)
			return static_cast<KeyCode>(nativeKey - GLFW_KEY_A + static_cast<int>(KeyCode::A));
		if (nativeKey >= GLFW_KEY_0 && nativeKey <= GLFW_KEY_9)
			return static_cast<KeyCode>(nativeKey - GLFW_KEY_0 + static_cast<int>(KeyCode::Num0));

		switch (nativeKey) {
		case GLFW_KEY_ESCAPE:       return KeyCode::Escape;
		case GLFW_KEY_ENTER:        return KeyCode::Enter;
		case GLFW_KEY_TAB:          return KeyCode::Tab;
		case GLFW_KEY_SPACE:        return KeyCode::Space;
		case GLFW_KEY_BACKSPACE:    return KeyCode::Backspace;
		case GLFW_KEY_LEFT_SHIFT:   return KeyCode::LeftShift;
		case GLFW_KEY_RIGHT_SHIFT:  return KeyCode::RightShift;
		case GLFW_KEY_LEFT_CONTROL: return KeyCode::LeftCtrl;
		case GLFW_KEY_RIGHT_CONTROL:return KeyCode::RightCtrl;
		case GLFW_KEY_LEFT_ALT:     return KeyCode::LeftAlt;
		case GLFW_KEY_RIGHT_ALT:    return KeyCode::RightAlt;
		case GLFW_KEY_UP:           return KeyCode::Up;
		case GLFW_KEY_DOWN:         return KeyCode::Down;
		case GLFW_KEY_LEFT:         return KeyCode::Left;
		case GLFW_KEY_RIGHT:        return KeyCode::Right;
		case GLFW_KEY_F1:           return KeyCode::F1;
		case GLFW_KEY_F2:           return KeyCode::F2;
		case GLFW_KEY_F3:           return KeyCode::F3;
		case GLFW_KEY_F4:           return KeyCode::F4;
		case GLFW_KEY_F5:           return KeyCode::F5;
		case GLFW_KEY_F6:           return KeyCode::F6;
		case GLFW_KEY_F7:           return KeyCode::F7;
		case GLFW_KEY_F8:           return KeyCode::F8;
		case GLFW_KEY_F9:           return KeyCode::F9;
		case GLFW_KEY_F10:          return KeyCode::F10;
		case GLFW_KEY_F11:          return KeyCode::F11;
		case GLFW_KEY_F12:          return KeyCode::F12;
		case GLFW_KEY_GRAVE_ACCENT: return KeyCode::GraveAccent;
		case GLFW_KEY_MINUS:        return KeyCode::Minus;
		case GLFW_KEY_EQUAL:        return KeyCode::Equal;
		case GLFW_KEY_LEFT_BRACKET: return KeyCode::LeftBracket;
		case GLFW_KEY_RIGHT_BRACKET:return KeyCode::RightBracket;
		case GLFW_KEY_SEMICOLON:    return KeyCode::Semicolon;
		case GLFW_KEY_APOSTROPHE:   return KeyCode::Apostrophe;
		case GLFW_KEY_COMMA:        return KeyCode::Comma;
		case GLFW_KEY_PERIOD:       return KeyCode::Period;
		case GLFW_KEY_SLASH:        return KeyCode::Slash;
		case GLFW_KEY_BACKSLASH:    return KeyCode::Backslash;
		default:                    return KeyCode::COUNT; // δʶ��ļ�
		}
	}

	MouseCode GlfwInput::GLFWMouseToEngine(int nativeButton) {
		switch (nativeButton) {
		case GLFW_MOUSE_BUTTON_LEFT:   return MouseCode::ButtonLeft;
		case GLFW_MOUSE_BUTTON_RIGHT:  return MouseCode::ButtonRight;
		case GLFW_MOUSE_BUTTON_MIDDLE: return MouseCode::ButtonMiddle;
		case GLFW_MOUSE_BUTTON_4:      return MouseCode::Button4;
		case GLFW_MOUSE_BUTTON_5:      return MouseCode::Button5;
		case GLFW_MOUSE_BUTTON_6:      return MouseCode::Button6;
		case GLFW_MOUSE_BUTTON_7:      return MouseCode::Button7;
		case GLFW_MOUSE_BUTTON_8:      return MouseCode::Button8;
		default:                       return MouseCode::COUNT;
		}
	}

	GlfwInput::GlfwInput(GLFWwindow* window)
		: m_Window(window)
	{
		std::memset(m_KeyStates, 0, sizeof(m_KeyStates));
		std::memset(m_MouseStates, 0, sizeof(m_MouseStates));
	}

	bool GlfwInput::IsKeyDown(KeyCode key) const {
		size_t idx = static_cast<size_t>(key);
		return idx < static_cast<size_t>(KeyCode::COUNT) && (m_KeyStates[idx] & FLAG_DOWN);
	}

	bool GlfwInput::IsKeyPressed(KeyCode key) const {
		size_t idx = static_cast<size_t>(key);
		return idx < static_cast<size_t>(KeyCode::COUNT)
			&& (m_KeyStates[idx] & FLAG_DOWN)
			&& (m_KeyStates[idx] & FLAG_CHANGED);
	}

	bool GlfwInput::IsKeyReleased(KeyCode key) const {
		size_t idx = static_cast<size_t>(key);
		return idx < static_cast<size_t>(KeyCode::COUNT)
			&& !(m_KeyStates[idx] & FLAG_DOWN)
			&& (m_KeyStates[idx] & FLAG_CHANGED);
	}

	bool GlfwInput::IsMouseButtonDown(MouseCode button) const {
		size_t idx = static_cast<size_t>(button);
		return idx < static_cast<size_t>(MouseCode::COUNT) && (m_MouseStates[idx] & FLAG_DOWN);
	}

	bool GlfwInput::IsMouseButtonPressed(MouseCode button) const {
		size_t idx = static_cast<size_t>(button);
		return idx < static_cast<size_t>(MouseCode::COUNT)
			&& (m_MouseStates[idx] & FLAG_DOWN)
			&& (m_MouseStates[idx] & FLAG_CHANGED);
	}

	bool GlfwInput::IsMouseButtonReleased(MouseCode button) const {
		size_t idx = static_cast<size_t>(button);
		return idx < static_cast<size_t>(MouseCode::COUNT)
			&& !(m_MouseStates[idx] & FLAG_DOWN)
			&& (m_MouseStates[idx] & FLAG_CHANGED);
	}

	void GlfwInput::OnUpdate() {

		for (auto& state : m_KeyStates)
			state &= FLAG_DOWN;
		for (auto& state : m_MouseStates)
			state &= FLAG_DOWN;
		m_MouseDeltaX = 0.0f;
		m_MouseDeltaY = 0.0f;
		m_ScrollDelta = 0.0f;
	}

	void GlfwInput::OnKeyEvent(int nativeKey, int action) {
		KeyCode key = GLFWKeyToEngine(nativeKey);
		if (key == KeyCode::COUNT) return; 

		size_t idx = static_cast<size_t>(key);
		if (action == GLFW_PRESS || action == GLFW_REPEAT) {
			m_KeyStates[idx] = FLAG_DOWN | FLAG_CHANGED;
		}
		else { // GLFW_RELEASE
			m_KeyStates[idx] = FLAG_CHANGED; // down=0, changed=1
		}
	}

	void GlfwInput::OnMouseButtonEvent(int nativeButton, int action) {
		MouseCode button = GLFWMouseToEngine(nativeButton);
		if (button == MouseCode::COUNT) return;

		size_t idx = static_cast<size_t>(button);
		if (action == GLFW_PRESS) {
			m_MouseStates[idx] = FLAG_DOWN | FLAG_CHANGED;
		}
		else { // GLFW_RELEASE
			m_MouseStates[idx] = FLAG_CHANGED;
		}
	}

	void GlfwInput::OnMouseMove(float x, float y) {
		m_MouseDeltaX = x - m_MouseX;
		m_MouseDeltaY = y - m_MouseY;
		m_MouseX = x;
		m_MouseY = y;
	}

	void GlfwInput::OnScroll(float xOffset, float yOffset) {
		m_ScrollDelta += yOffset;
	}
	IInput* Input::s_Instance = nullptr;
	std::unique_ptr<IInput> Input::s_InstanceOwner = nullptr;
	bool Input::s_BlockMouse = false;
	bool Input::s_BlockKeyboard = false;

	void Input::Init(std::unique_ptr<IInput> instance) {
		s_InstanceOwner = std::move(instance);
		s_Instance = s_InstanceOwner.get();
	}

	void Input::Shutdown() {
		s_Instance = nullptr;
		s_InstanceOwner.reset();
	}

}