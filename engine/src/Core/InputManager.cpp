#include "Engine/Core/InputManager.h"
#include "Engine/Core/IWindow.h"
#include "Engine/Platform/GlfwInput.h"
#include "Engine/Core/Renderer/OrthographicCamera.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

namespace Engine {

    // ── 辅助：Engine Vec2 ↔ glm::vec2 ──
    namespace {
        inline glm::vec2 ToGlm(const Vec2& v) { return glm::vec2(v.x, v.y); }
        inline Vec2 FromGlm(const glm::vec2& v) { return Vec2(v.x, v.y); }
    }

    void InputManager::Init(IWindow* window) {
        if (m_Initialized) {
            std::cerr << "[InputManager] Already initialized, skipping." << std::endl;
            return;
        }
        auto* nativeWin = static_cast<GLFWwindow*>(window->GetNativeHandle());
        Input::Init(std::make_unique<GlfwInput>(nativeWin));
        m_Initialized = true;
        std::cout << "[InputManager] Initialized." << std::endl;
    }

    void InputManager::Shutdown() {
        m_Actions.clear();
        Input::Shutdown();
        m_Initialized = false;
        std::cout << "[InputManager] Shutdown." << std::endl;
    }


    void InputManager::OnUpdate() {
        if (Input::Get()) {
            Input::Get()->OnUpdate();
        }

        for (auto& [name, action] : m_Actions) {
            action->Update();
        }
    }


    bool InputManager::IsKeyDown(KeyCode key) const {
        return Input::IsKeyDown(key);
    }
    bool InputManager::IsKeyPressed(KeyCode key) const {
        return Input::IsKeyPressed(key);
    }
    bool InputManager::IsKeyReleased(KeyCode key) const {
        return Input::IsKeyReleased(key);
    }

    bool InputManager::IsMouseDown(MouseCode button) const {
        return Input::IsMouseButtonDown(button);
    }
    bool InputManager::IsMousePressed(MouseCode button) const {
        return Input::IsMouseButtonPressed(button);
    }
    bool InputManager::IsMouseReleased(MouseCode button) const {
        return Input::IsMouseButtonReleased(button);
    }

    float32 InputManager::GetMouseX() const { return Input::GetMouseX(); }
    float32 InputManager::GetMouseY() const { return Input::GetMouseY(); }
    float32 InputManager::GetMouseDeltaX() const { return Input::GetMouseDeltaX(); }
    float32 InputManager::GetMouseDeltaY() const { return Input::GetMouseDeltaY(); }
    float32 InputManager::GetScrollDelta() const { return Input::GetScrollDelta(); }


    InputAction& InputManager::CreateAction(const std::string& name) {
        auto it = m_Actions.find(name);
        if (it != m_Actions.end()) {
            return *(it->second);
        }
        auto action = std::make_unique<InputAction>(name);
        InputAction& ref = *action;
        m_Actions[name] = std::move(action);
        return ref;
    }

    void InputManager::RemoveAction(const std::string& name) {
        m_Actions.erase(name);
    }

    InputAction* InputManager::GetAction(const std::string& name) {
        auto it = m_Actions.find(name);
        return (it != m_Actions.end()) ? it->second.get() : nullptr;
    }

    bool InputManager::IsActionDown(const std::string& name) const {
        auto it = m_Actions.find(name);
        return (it != m_Actions.end()) && it->second->IsDown();
    }

    bool InputManager::IsActionPressed(const std::string& name) const {
        auto it = m_Actions.find(name);
        return (it != m_Actions.end()) && it->second->IsPressed();
    }

    bool InputManager::IsActionReleased(const std::string& name) const {
        auto it = m_Actions.find(name);
        return (it != m_Actions.end()) && it->second->IsReleased();
    }

    // ── 屏幕 → 世界坐标转换 ─────────────────────────────────────
    Vec2 InputManager::ScreenToWorld(
        const OrthographicCamera& camera,
        float32 windowWidth, float32 windowHeight,
        float32 mouseX, float32 mouseY) const
    {
        // 1. 屏幕坐标 → NDC [-1, 1]
        float32 ndcX = (2.0f * mouseX) / windowWidth - 1.0f;
        float32 ndcY = 1.0f - (2.0f * mouseY) / windowHeight;  // 翻转 Y

        // 2. 获取视图投影矩阵的逆矩阵
        glm::mat4 viewProjection = glm::make_mat4(camera.GetViewProjectionMatrixPtr());
        glm::mat4 invVP = glm::inverse(viewProjection);

        // 3. NDC → 世界坐标
        glm::vec4 worldPos = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        worldPos /= worldPos.w;

        return Vec2(worldPos.x, worldPos.y);
    }

    Vec2 InputManager::ScreenToWorld(
        const OrthographicCamera& camera,
        float32 windowWidth, float32 windowHeight) const
    {
        return ScreenToWorld(camera, windowWidth, windowHeight,
            Input::GetMouseX(), Input::GetMouseY());
    }

    // ── 绑定持久化（简易 JSON 格式） ────────────────────────────
    void InputManager::SaveBindings(const std::string& filePath) const {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "[InputManager] Failed to save bindings to: " << filePath << std::endl;
            return;
        }

        file << "{\n";
        bool first = true;
        for (const auto& [name, action] : m_Actions) {
            if (!first) file << ",\n";
            first = false;
            file << "  \"" << name << "\": [\n";
            bool firstBinding = true;
            for (const auto& binding : action->GetBindings()) {
                if (!firstBinding) file << ",\n";
                firstBinding = false;
                file << "    { \"type\": \""
                    << (binding.type == KeyBinding::Type::Keyboard ? "key" : "mouse")
                    << "\", \"code\": "
                    << (binding.type == KeyBinding::Type::Keyboard
                        ? std::to_string(static_cast<int>(binding.keyCode))
                        : std::to_string(static_cast<int>(binding.mouseCode)))
                    << " }";
            }
            file << "\n  ]";
        }
        file << "\n}\n";

        std::cout << "[InputManager] Bindings saved to: " << filePath << std::endl;
    }

    void InputManager::LoadBindings(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "[InputManager] Failed to load bindings from: " << filePath << std::endl;
            return;
        }

        // 简单的 JSON 解析（仅用于演示，生产环境建议用 nlohmann/json 等库）
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        std::cout << "[InputManager] Bindings loaded from: " << filePath << std::endl;
        std::cout << "[InputManager] Full JSON parser not implemented in this demo." << std::endl;
        std::cout << "[InputManager] Content preview: " << content.substr(0, 100) << "..." << std::endl;
    }

} // namespace Engine
