#pragma once

/**
 * @file _3DTestApp.h
 * @brief 3D 图形调试测试应用
 *
 * 独立应用，参照 ComplexSceneTestApp 架构。
 * 包含完整的 3D 场景（方块/球体/圆柱/平面）、菜单系统、
 * 调试面板集成、GPU Profiler、ViewModes、辅助可视化。
 * 
 * 【修复】修复闪烁、去除硬编码Title、添加物体运动与碰撞检测、
 * DebugLightState 持久化光源编辑。
 */

#include "Engine/ConsolePanel.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/MeshComponent.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IWindow.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/InputManager.h"
#include "Engine/Core/MenuManager.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Renderer/Mesh.h"
#include "Engine/Core/Renderer/PerspectiveCamera.h"
#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include "Engine/Core/RHI/MeshRenderer.h"
#include "Engine/Core/RHI/ShadowMapper.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/MemoryPanel.h"
#include "Engine/PerformanceWindow.h"
#include "Engine/Types.h"
#include "Engine/UIManager.h"

#include <imgui.h>
#include <memory>
#include <vector>
#include <string>

namespace Engine {

    class _3DTestApp {
    public:
        _3DTestApp(IGraphicsFactory& factory, const char* title);
        ~_3DTestApp();

        void Run();

    private:
        bool InitUI();
        void BuildScene();
        void HandleInput(float dt);
        void UpdateLogic(float dt);
        void RenderHelperVisualizations();
        void DrawGrid(float size, int steps);
        void DrawOriginAxis();
        void DrawDebugImGui();
        void OnWindowResize(int width, int height);

        // 调试数据填充
        void PopulateLightingDebugData();
        void PopulateGeometryDebugData();
        void PopulatePostProcessingDebugData();
        void PopulateTextureDebugData();

        // 同步 DebugLightState -> MeshRenderer
        void SyncDebugLights();

        IGraphicsFactory& m_Factory;
        std::unique_ptr<IWindow> m_Window;
        InputManager m_InputManager;
        TextureManager m_TextureManager;

        std::unique_ptr<MeshRenderer> m_MeshRenderer;
        std::unique_ptr<ShadowMapper> m_ShadowMapper;
        std::shared_ptr<Shader> m_3DShader;
        std::shared_ptr<Shader> m_DepthShader;

        PerspectiveCamera m_Camera;
        float m_CameraSpeed = 10.0f;
        float m_MouseSensitivity = 0.15f;

        Scene m_Scene;

        PerformanceWindow m_PerfWindow;
        ConsolePanel m_ConsolePanel;
        MenuManager m_MenuManager;
        bool m_UIInitialized = false;

        std::vector<GameObject*> m_SceneObjects;
        
        // 碰撞与运动追踪对象
        GameObject* m_MovingObj = nullptr;
        MeshComponent* m_MovingMeshComp = nullptr;
        std::vector<GameObject*> m_Colliders;
        Vec3 m_MovingVelocity = { 8.0f, 0.0f, 6.0f };

        bool m_AnimateLights = true;
        float m_LightAngle = 0.0f;
        uint32 m_FrameCount = 0;

        // 菜单状态
        bool m_GameStarted = false;

        // 应用标题（从外部传入，避免硬编码）
        const char* m_AppTitle = nullptr;

        // ── 持久化调试光源状态（避免 UI 修改被 Populate 覆盖） ──
        struct DebugLightState {
            Vec3  position;
            Vec3  color;
            float intensity;
            bool  dirty = true;
        };
        std::vector<DebugLightState> m_DebugLights;
    };

} // namespace Engine