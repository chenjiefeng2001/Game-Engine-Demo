#include "Engine/Core/InputAction.h"

namespace Engine {

    // ── KeyBinding ────────────────────────────────────────────────
    bool KeyBinding::operator==(const KeyBinding& other) const {
        if (type != other.type) return false;
        if (type == Type::Keyboard) return keyCode == other.keyCode;
        return mouseCode == other.mouseCode;
    }

    // ── InputAction ──────────────────────────────────────────────
    InputAction::InputAction(std::string name)
        : m_Name(std::move(name))
    {
    }

    void InputAction::AddBinding(const KeyBinding& binding) {
        // 避免重复添加
        for (const auto& b : m_Bindings) {
            if (b == binding) return;
        }
        m_Bindings.push_back(binding);
    }

    void InputAction::RemoveBinding(const KeyBinding& binding) {
        for (auto it = m_Bindings.begin(); it != m_Bindings.end(); ++it) {
            if (*it == binding) {
                m_Bindings.erase(it);
                return;
            }
        }
    }

    void InputAction::ClearBindings() {
        m_Bindings.clear();
    }

    bool InputAction::IsDown() const {
        for (const auto& binding : m_Bindings) {
            if (binding.type == KeyBinding::Type::Keyboard) {
                if (Input::IsKeyDown(binding.keyCode)) return true;
            }
            else {
                if (Input::IsMouseButtonDown(binding.mouseCode)) return true;
            }
        }
        return false;
    }

    bool InputAction::IsPressed() const {
        for (const auto& binding : m_Bindings) {
            if (binding.type == KeyBinding::Type::Keyboard) {
                if (Input::IsKeyPressed(binding.keyCode)) return true;
            }
            else {
                if (Input::IsMouseButtonPressed(binding.mouseCode)) return true;
            }
        }
        return false;
    }

    bool InputAction::IsReleased() const {
        for (const auto& binding : m_Bindings) {
            if (binding.type == KeyBinding::Type::Keyboard) {
                if (Input::IsKeyReleased(binding.keyCode)) return true;
            }
            else {
                if (Input::IsMouseButtonReleased(binding.mouseCode)) return true;
            }
        }
        return false;
    }

    void InputAction::InvokeCallbacks() const {
        // 处理 Pressed 回调
        if (IsPressed()) {
            for (const auto& cb : m_PressedCallbacks) {
                if (cb) cb();
            }
        }
        // 处理 Released 回调
        if (IsReleased()) {
            for (const auto& cb : m_ReleasedCallbacks) {
                if (cb) cb();
            }
        }
    }

    void InputAction::Update() {
        bool isDown = IsDown();

        // 触发按下的回调
        if (isDown && !m_WasDownLastFrame) {
            for (const auto& cb : m_PressedCallbacks) {
                if (cb) cb();
            }
        }
        // 触发释放的回调
        if (!isDown && m_WasDownLastFrame) {
            for (const auto& cb : m_ReleasedCallbacks) {
                if (cb) cb();
            }
        }

        m_WasDownLastFrame = isDown;
    }

} // namespace Engine
