#pragma once
#include "Engine/Core/Input.h"
#include <glad/gl.h>
#include <stdint.h>

struct GLFWwindow;

namespace Engine {
    class GlfwInput : public IInput {
    public:
        GlfwInput(GLFWwindow* window);
        virtual ~GlfwInput() override = default;


        virtual bool IsKeyDown(KeyCode key) const override;
        virtual bool IsKeyPressed(KeyCode key) const override;
        virtual bool IsKeyReleased(KeyCode key) const override;

        virtual bool IsMouseButtonDown(MouseCode button) const override;
        virtual bool IsMouseButtonPressed(MouseCode button) const override;
        virtual bool IsMouseButtonReleased(MouseCode button) const override;

        virtual float GetMouseX() const override { return m_MouseX; }
        virtual float GetMouseY() const override { return m_MouseY; }
        virtual float GetMouseDeltaX() const override { return m_MouseDeltaX; }
        virtual float GetMouseDeltaY() const override { return m_MouseDeltaY; }
        virtual float GetScrollDelta() const override { return m_ScrollDelta; }

        virtual void OnUpdate() override;

        virtual void OnKeyEvent(int nativeKey, int action) override;
        virtual void OnMouseButtonEvent(int nativeButton, int action) override;
        virtual void OnMouseMove(float x, float y) override;
        virtual void OnScroll(float xOffset, float yOffset) override;

    private:
        GLFWwindow* m_Window;

        static constexpr uint8_t FLAG_DOWN = 1 << 0;
        static constexpr uint8_t FLAG_CHANGED = 1 << 1;

        uint8_t m_KeyStates[static_cast<size_t>(KeyCode::COUNT)]{};
        uint8_t m_MouseStates[static_cast<size_t>(MouseCode::COUNT)]{};

        float m_MouseX = 0.0f, m_MouseY = 0.0f;
        float m_MouseDeltaX = 0.0f, m_MouseDeltaY = 0.0f;
        float m_ScrollDelta = 0.0f;
        static KeyCode GLFWKeyToEngine(int nativeKey);
        static MouseCode GLFWMouseToEngine(int nativeButton);
    };

}