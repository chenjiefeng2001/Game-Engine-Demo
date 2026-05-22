#pragma once
#include "Engine/Core/Input.h"
#include "Engine/Core/InputAction.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <memory>

struct GLFWwindow;

namespace Engine {

	class OrthographicCamera;
	class IWindow;

	class InputManager {
	public:
		InputManager() = default;
		~InputManager() = default;
		void Init(GLFWwindow* window);  
		void Shutdown();

		void OnUpdate(); 
		bool IsKeyDown(KeyCode key) const;
		bool IsKeyPressed(KeyCode key) const;
		bool IsKeyReleased(KeyCode key) const;

		bool IsMouseDown(MouseCode button) const;
		bool IsMousePressed(MouseCode button) const;
		bool IsMouseReleased(MouseCode button) const;

		float GetMouseX() const;
		float GetMouseY() const;
		float GetMouseDeltaX() const;
		float GetMouseDeltaY() const;
		float GetScrollDelta() const;


		InputAction& CreateAction(const std::string& name);
		void RemoveAction(const std::string& name);
		InputAction* GetAction(const std::string& name);

		bool IsActionDown(const std::string& name) const;
		bool IsActionPressed(const std::string& name) const;
		bool IsActionReleased(const std::string& name) const;


		glm::vec2 ScreenToWorld(const OrthographicCamera& camera,
			float windowWidth, float windowHeight,
			float mouseX, float mouseY) const;
		glm::vec2 ScreenToWorld(const OrthographicCamera& camera,
			float windowWidth, float windowHeight) const;

		void SaveBindings(const std::string& filePath) const;
		void LoadBindings(const std::string& filePath);

	private:
		std::unordered_map<std::string, std::unique_ptr<InputAction>> m_Actions;
		bool m_Initialized = false;
	};

} // namespace Engine