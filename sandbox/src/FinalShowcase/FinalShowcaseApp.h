#pragma once

#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/Renderer/PerspectiveCamera.h>
#include <Engine/Core/RHI/MeshRenderer.h>
#include <Engine/Core/RHI/RHIBackend.h>
#include <Engine/Rendering/ShadowMapper.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/MeshComponent.h>
#include <Engine/Core/Renderer/Mesh.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/PerformanceWindow.h>
#include <imgui.h>

#include <memory>
#include <vector>

namespace Engine {

class FinalShowcaseApp {
public:
    FinalShowcaseApp(IGraphicsFactory& factory);
    ~FinalShowcaseApp();
    void Run();

private:
    IGraphicsFactory& m_Factory;
    std::unique_ptr<IWindow> m_Window;
    InputManager m_InputMgr;

    std::unique_ptr<PerspectiveCamera> m_Camera;
    std::unique_ptr<Scene> m_Scene;
    std::unique_ptr<MeshRenderer> m_MeshRenderer;
    std::shared_ptr<Shader> m_Shader3D;
    std::unique_ptr<Rendering::ShadowMapper> m_ShadowMapper;

    PerformanceWindow m_PerfWindow;

    std::vector<GameObject*> m_SceneObjects;
    uint64 m_FrameCount = 0;
    bool m_Running = true;

    // ── 相机 ──
    float m_CameraSpeed = 12.0f;
    float m_MouseSensitivity = 0.2f;
    bool m_RightDragging = false;
    double m_LastMouseX = 0.0, m_LastMouseY = 0.0;
    float m_StoredPitch = -25.0f;
    float m_StoredYaw = 0.0f;

    void BuildScene();
    void HandleInput(float dt);
    void DrawDebugPanel();
    void OnWindowResize(int w, int h);
};

} // namespace Engine