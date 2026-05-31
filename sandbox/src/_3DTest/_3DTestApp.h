#pragma once

#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/Renderer/PerspectiveCamera.h>
#include <Engine/Core/RHI/SceneRenderer.h>
#include <Engine/Core/RHI/MeshRenderer.h>
#include <Engine/Core/RHI/IRenderQueue.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/MeshComponent.h>
#include <Engine/ConsolePanel.h>
#include <Engine/UIManager.h>
#include <Engine/PerformanceWindow.h>
#include <Engine/MemoryPanel.h>
#include <Engine/Core/RHI/PotentiallyVisibleSet.h>
#include <Engine/Types.h>

#include <memory>
#include <vector>
#include <imgui.h>

namespace Engine {

    class _3DTestApp {
    public:
        _3DTestApp(IGraphicsFactory& factory);
        ~_3DTestApp() = default;

        void Run();

    private:
        bool InitUI();
        void BuildScene();
        void HandleInput(float dt);
        void RenderCoordinateAxes();
        void RenderGrid();
        void DrawDebugImGui();

        IGraphicsFactory&       m_Factory;
        std::unique_ptr<IWindow> m_Window;
        InputManager            m_InputManager;
        TextureManager          m_TextureManager;
        SceneRenderer           m_SceneRenderer;
        PerspectiveCamera       m_Camera;

        std::shared_ptr<MeshRenderer> m_MeshRenderer;
        std::shared_ptr<Shader> m_3DShader;

        // 坐标轴 / 网格调试资源
        std::shared_ptr<class VertexArray> m_AxesVAO;
        std::shared_ptr<class VertexBuffer> m_AxesVBO;
        uint32 m_AxesIndexCount = 0;
        std::shared_ptr<class VertexArray> m_GridVAO;
        std::shared_ptr<class VertexBuffer> m_GridVBO;
        uint32 m_GridIndexCount = 0;

        Scene m_Scene;

        // 控制台 / 性能 / 内存
        ConsolePanel      m_ConsolePanel;
        PerformanceWindow m_PerfWindow;
        MemoryPanel       m_MemoryPanel;
        bool m_UIInitialized = false;

        // 相机控制
        float m_CameraSpeed = 5.0f;
        float m_MouseSensitivity = 0.1f;
        bool m_MouseCaptured = false;

        // 调试开关
        bool m_ShowAxes = true;
        bool m_ShowGrid = true;

        // ── 潜在可见集 (PVS) ──
        PotentiallyVisibleSet m_PVS;
        bool m_PVSEnabled = true;
        bool m_PVSDebugDraw = false;
        Vec3 m_PVSBBoxMin = {-12.0f, -5.0f, -12.0f};
        Vec3 m_PVSBBoxMax = {12.0f, 10.0f, 12.0f};
        Vec3 m_PVSCellSize = {4.0f, 4.0f, 4.0f};

        // ── 抗锯齿 ──
        AntiAliasingConfig m_AADemoConfig;
        int m_CurrentAAMode = 0;     // 0=None, 1=MSAA, 2=SSAA, 3=CSAA, 4=MLAA
        int m_AASampleIndex = 1;     // Combo 索引: 0=2x, 1=4x, 2=8x
        static constexpr int k_SampleValues[3] = { 2, 4, 8 };
        float m_AASCale = 1.5f;
    };

} // namespace Engine
