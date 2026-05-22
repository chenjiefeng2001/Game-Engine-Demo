#pragma once
#include <memory>
#include <cstdint>
namespace Engine {

    
    enum class KeyCode : uint16_t {
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

    enum class MouseCode : uint8_t {
        ButtonLeft = 0,
        ButtonRight,
        ButtonMiddle,
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

        static bool IsKeyDown(KeyCode key) { return s_Instance && s_Instance->IsKeyDown(key); }
        static bool IsKeyPressed(KeyCode key) { return s_Instance && s_Instance->IsKeyPressed(key); }
        static bool IsKeyReleased(KeyCode key) { return s_Instance && s_Instance->IsKeyReleased(key); }

        static bool IsMouseButtonDown(MouseCode b) { return s_Instance && s_Instance->IsMouseButtonDown(b); }
        static bool IsMouseButtonPressed(MouseCode b) { return s_Instance && s_Instance->IsMouseButtonPressed(b); }
        static bool IsMouseButtonReleased(MouseCode b) { return s_Instance && s_Instance->IsMouseButtonReleased(b); }

        static float GetMouseX() { return s_Instance ? s_Instance->GetMouseX() : 0.0f; }
        static float GetMouseY() { return s_Instance ? s_Instance->GetMouseY() : 0.0f; }
        static float GetMouseDeltaX() { return s_Instance ? s_Instance->GetMouseDeltaX() : 0.0f; }
        static float GetMouseDeltaY() { return s_Instance ? s_Instance->GetMouseDeltaY() : 0.0f; }
        static float GetScrollDelta() { return s_Instance ? s_Instance->GetScrollDelta() : 0.0f; }

    private:
        static IInput* s_Instance;
        static std::unique_ptr<IInput> s_InstanceOwner;
    };

}