#pragma once
#include "Engine/Core/Input.h"
#include "Engine/Core/InputAction.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace Engine {

	class OrthographicCamera;
	class IWindow;

	class InputManager {
	public:
		InputManager() = default;
		~InputManager() = default;
		void Init(IWindow* window);
		void Shutdown();

		void OnUpdate();
		bool IsKeyDown(KeyCode key) const;
		bool IsKeyPressed(KeyCode key) const;
		bool IsKeyReleased(KeyCode key) const;

		bool IsMouseDown(MouseCode button) const;
		bool IsMousePressed(MouseCode button) const;
		bool IsMouseReleased(MouseCode button) const;

		float32 GetMouseX() const;
		float32 GetMouseY() const;
		float32 GetMouseDeltaX() const;
		float32 GetMouseDeltaY() const;
		float32 GetScrollDelta() const;


		InputAction& CreateAction(const std::string& name);
		void RemoveAction(const std::string& name);
		InputAction* GetAction(const std::string& name);

		bool IsActionDown(const std::string& name) const;
		bool IsActionPressed(const std::string& name) const;
		bool IsActionReleased(const std::string& name) const;


		Vec2 ScreenToWorld(const OrthographicCamera& camera,
			float32 windowWidth, float32 windowHeight,
			float32 mouseX, float32 mouseY) const;
		Vec2 ScreenToWorld(const OrthographicCamera& camera,
			float32 windowWidth, float32 windowHeight) const;

		void SaveBindings(const std::string& filePath) const;
		void LoadBindings(const std::string& filePath);

	private:
		std::unordered_map<std::string, std::unique_ptr<InputAction>> m_Actions;
		bool m_Initialized = false;
	};

} // namespace Engine