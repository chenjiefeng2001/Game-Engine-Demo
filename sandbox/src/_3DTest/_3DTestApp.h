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

// 新渲染系统头文件
#include "Engine/Rendering/LightTypes.h"
#include "Engine/Rendering/PostProcessPipeline.h"
#include "Engine/Rendering/DeferredLightingPass.h"
#include "Engine/Rendering/CompactGBuffer.h"
#include "Engine/Rendering/RenderGraph.h"
#include "Engine/Rendering/TransientHeap.h"

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
        void DrawRenderPipelineDebugPanel();
        void DrawPBRDebugPanel();
        void DrawPostProcessDebugPanel();
        void DrawCSMDebugPanel();
        void OnWindowResize(int width, int height);

        // 调试数据填充
        void PopulateLightingDebugData();
        void PopulateGeometryDebugData();
        void PopulatePostProcessingDebugData();
        void PopulateTextureDebugData();

        // 同步 DebugLightState -> MeshRenderer
        void SyncDebugLights();

        // 新渲染管线初始化
        bool InitDeferredRendering();
        void RenderDeferredTest();

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

        // ════════════════════════════════════════════════════════
        // 新渲染管线 — 调试控制状态
        // ════════════════════════════════════════════════════════

        // 渲染模式选择
        enum class RenderMode : int {
            ForwardLit    = 0,    // 当前的前向渲染
            DeferredLit   = 1,    // 延迟渲染 (PBR + CSM)
            GBufferAlbedo = 2,    // 仅显示 GBuffer Albedo
            GBufferNormal = 3,    // 仅显示 GBuffer 法线
            GBufferRoughness = 4, // 仅显示 GBuffer 粗糙度
            DepthOnly     = 5,    // 仅显示深度
        };
        RenderMode m_RenderMode = RenderMode::ForwardLit;

        // 新渲染系统组件
        Rendering::PostProcessPipeline m_PostProcess;
        Rendering::DeferredLightingPass m_DeferredLighting;
        Rendering::RenderGraph m_RenderGraph;
        Rendering::TransientHeap m_TransientHeap;

        // 相机 UBO 数据（用于延迟渲染 Pass）
        struct CameraUBO {
            Mat4 view;
            Mat4 proj;
            Mat4 invProj;
            Vec3 viewPos;
            float _pad0;
        } m_CameraUBO;

        // CSM ShadowMapper 实例
        class CSMShadowMapper* m_CSMShadowMapper = nullptr;

        // ── 延迟渲染 GPU 资源 ──
        std::unique_ptr<GBuffer> m_DeferredGBuffer;
        std::shared_ptr<Shader> m_DeferredGeomShader;   // deferred_geom (GBuffer Pass)
        std::shared_ptr<Shader> m_DeferredLightShader;  // deferred_light (Lighting Pass)
        std::shared_ptr<Shader> m_FullscreenQuadShader; // fullscreen_quad (全屏四边形)
        bool m_DeferredInitialized = false;
        bool m_GBufferCreated = false;

        // GBuffer 调试可视化参数
        int m_GBufferDebugTarget = 0;  // 0=Albedo, 1=Normal, 2=Roughness, 3=Depth

        // PBR 调试参数
        struct PBRDebugParams {
            float roughness  = 0.5f;
            float metallic   = 0.0f;
            float ao         = 1.0f;
            float normalStrength = 1.0f;
            float exposure   = 1.0f;
            float gamma      = 2.2f;
            bool  showBRDF   = false;
        } m_PBRDebug;

        // 后处理调试控制
        struct PostProcessDebug {
            bool  enabled       = true;
            bool  bloomEnabled  = true;
            float bloomIntensity = 1.2f;
            float bloomThreshold = 1.0f;
            bool  taaEnabled    = false;
            float taaBlendFactor = 0.95f;
            bool  fxaaEnabled   = true;
            int   toneMapMode   = 2;  // 0=None 1=Reinhard 2=ACES 3=Unreal 4=Filmic
            float exposure      = 1.0f;
            float gamma         = 2.2f;
            bool  showDebugOverlay = false;
        } m_PostDebug;

        // CSM 调试控制
        struct CSMDebug {
            bool  enabled      = false;
            bool  showCascades = false;
            int   cascadeCount = 4;
            float splitLambda  = 0.95f;
            float shadowBias   = 0.005f;
            int   shadowMapSize = 2048;
        } m_CSMDebug;

        bool m_UseRenderGraph = false;  // 使用 RenderGraph 执行管线
    };

} // namespace Engine