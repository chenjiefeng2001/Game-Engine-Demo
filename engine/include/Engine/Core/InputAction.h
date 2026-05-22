#pragma once
#include "Engine/Core/Input.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace Engine {

	struct KeyBinding {
		enum class Type { Keyboard, Mouse };
		Type type;
		union {
			KeyCode keyCode;
			MouseCode mouseCode;
		};

		static KeyBinding FromKey(KeyCode k) {
			KeyBinding b{ Type::Keyboard };
			b.keyCode = k;
			return b;
		}
		static KeyBinding FromMouse(MouseCode m) {
			KeyBinding b{ Type::Mouse };
			b.mouseCode = m;
			return b;
		}

		bool operator==(const KeyBinding& other) const;
	};

	class InputAction {
	public:
		explicit InputAction(std::string name);

		const std::string& GetName() const { return m_Name; }

		void AddBinding(const KeyBinding& binding);
		void RemoveBinding(const KeyBinding& binding);
		void ClearBindings();
		const std::vector<KeyBinding>& GetBindings() const { return m_Bindings; }

		bool IsDown() const;
		bool IsPressed() const;
		bool IsReleased() const;

		using Callback = std::function<void()>;
		void OnPressed(Callback cb) { m_PressedCallbacks.push_back(std::move(cb)); }
		void OnReleased(Callback cb) { m_ReleasedCallbacks.push_back(std::move(cb)); }
		void InvokeCallbacks() const;

		void Update();

	private:
		std::string m_Name;
		std::vector<KeyBinding> m_Bindings;
		std::vector<Callback> m_PressedCallbacks;
		std::vector<Callback> m_ReleasedCallbacks;
		bool m_WasDownLastFrame = false;
	};

} // namespace Engine