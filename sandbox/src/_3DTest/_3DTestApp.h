#pragma once

/**
 * @file _3DTestApp.h
 * @brief 3D 测试应用 — 集成全部图形调试菜单的独立测试用例
 *
 * 此应用参照 ComplexSceneTestApp 的结构，不继承 Application，
 * 而是独立管理窗口、输入、渲染和所有调试面板。
 *
 * 演示功能：
 *   - 12 个 Graphics Debug Tools 面板的数据填充
 *   - GPU Pass 时间戳标记 (ShadowPass/BasePass/PostProcess)
 *   - ViewMode 切换、辅助可视化 (Grid/Bones/Colliders)
 *   - 暂停/单帧步进控制
 *   - 菜单系统 (开始游戏/继续/退出)
 *   - 控制台 (~ 键)
 *   - 视角操作 (WASD + 鼠标右键)
 */

#include "Engine/ConsolePanel.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IWindow.h"
#include "Engine/Core/Input.h"
#include "Engine/Core/InputManager.h"
#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include "Engine/Core/RHI/MeshRenderer.h"
#include "Engine/Core/RHI/ShadowMapper.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/Renderer/PerspectiveCamera.h"
#include "Engine/PerformanceWindow.h"
#include "Engine/Core/MenuManager.h"
#include "Engine/Types.h"
#include "Engine/UIManager.h"

#include <imgui.h>
#include <memory>
#include <vector>

namespace Engine {

    class _3DTestApp {
    public:
        _3DTestApp(IGraphicsFactory& factory);
        ~_3DTestApp();

        void Run();

    private:
        bool InitUI();

        // ── 调试数据填充 ──
        void PopulateLightingDebugData();
        void PopulateGeometryDebugData();
        void PopulatePostProcessingDebugData();
        void PopulateTextureDebugData();

        // ── 辅助可视化 ──
        void RenderHelperVisualizations();
        void DrawGrid(float size, int steps);
        void DrawOriginAxis();

        // ── 输入处理 ──
        void HandleInput(float dt);

        // ── ImGui 面板 ──
        void DrawDebugImGui();

        // ── 窗口事件 ──
        void OnWindowResize(int width, int height);

        IGraphicsFactory& m_Factory;
        std::unique_ptr<IWindow> m_Window;
        InputManager m_InputManager;
        TextureManager m_TextureManager;

        // ── 3D 渲染器 ──
        std::unique_ptr<MeshRenderer> m_MeshRenderer;
        std::unique_ptr<ShadowMapper> m_ShadowMapper;
        std::shared_ptr<Shader> m_3DShader;
        std::shared_ptr<Shader> m_DepthShader;

        // ── 相机 ──
        PerspectiveCamera m_Camera;
        float m_CameraSpeed = 10.0f;
        float m_MouseSensitivity = 0.15f;

        // ── 调试面板 ──
        PerformanceWindow m_PerfWindow;
        ConsolePanel m_ConsolePanel;
        MenuManager m_MenuManager;
        bool m_UIInitialized = false;

        // ── 场景物体 ──
        std::vector<GameObject*> m_SceneObjects;

        // ── 渲染控制 ──
        bool m_ParticlesEnabled = true;
        bool m_HUDVisible = true;
        float m_LightAngle = 0.0f;
        bool m_AnimateLights = true;

        // ── 帧计数 ──
        uint32 m_FrameCount = 0;
    };

} // namespace Engine