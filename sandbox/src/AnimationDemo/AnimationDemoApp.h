#pragma once

/**
 * @file AnimationDemoApp.h
 * @brief 3D 动画演示场景 — 展示完整的动画系统功能
 *
 * 场景内容：
 *   - 地面网格 + 坐标轴
 *   - 3 个程序化生成的角色（几何体组合）
 *   - 角色 1 "Walker": 行走机器人（AnimationController + 状态机）
 *   - 角色 2 "Bouncer": 弹跳生物（AnimationLocalTimeline + BlendTree）
 *   - 角色 3 "Looker": 注视者（LookAt 约束 + Point 约束）
 *   - ImGui 调试控制面板
 *
 * 演示的动画功能：
 *   ✓ AnimationController 层系统
 *   ✓ AnimStateMachine 状态过渡
 *   ✓ BlendTree 混合
 *   ✓ LocalTimeline 关键帧动画
 *   ✓ Skeleton + 骨骼层级
 *   ✓ Constraint 约束（LookAt + Point）
 *   ✓ 跨物体定位器
 */

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
#include <Engine/Types.h>
#include <Engine/Core/Renderer/Mesh.h>
#include <Engine/Core/RenderResources/Shader.h>

#include <Engine/Animation/AnimationController.h>
#include <Engine/Animation/ConstraintSolver.h>
#include <Engine/Animation/AnimationLocalTimeline.h>
#include <Engine/Animation/AnimationGlobalTimeline.h>
#include <Engine/Animation/Skeleton.h>
#include <Engine/Animation/AnimationBlend.h>
#include <Engine/Animation/AnimationPose.h>
#include <Engine/Animation/BlendTree.h>

#include <memory>
#include <vector>
#include <unordered_map>
#include <imgui.h>

namespace Engine {

    // ============================================================
    // 程序化角色
    // ============================================================
    struct AnimatedCharacter {
        std::string name;

        // 骨骼与动画
        std::shared_ptr<Skeleton> skeleton;
        std::shared_ptr<AnimationResource> resource;
        std::unique_ptr<AnimationController> controller;
        std::unique_ptr<ConstraintSolver> constraintSolver;

        // 用于渲染的"骨骼显示" — 每个骨骼对应一个几何体
        struct BoneDisplay {
            int32 boneIndex;
            std::shared_ptr<Mesh> mesh;
            Vec3 color;
            Vec3 offset;      // 相对于骨骼原点的偏移
            Vec3 scale;       // 显示缩放
            std::shared_ptr<GameObject> gameObject; ///< 关联的渲染对象
        };
        std::vector<BoneDisplay> displays;

        // 世界位置
        Vec3 worldPosition{0, 0, 0};
    };

    // ============================================================
    // 动画演示应用
    // ============================================================
    class AnimationDemoApp {
    public:
        AnimationDemoApp(IGraphicsFactory& factory);
        ~AnimationDemoApp();

        void Run();

    private:
        // ── 初始化 ──
        bool InitUI();
        void InitScene();

        // ── 角色创建 ──
        AnimatedCharacter* MakeWalker();
        AnimatedCharacter* MakeBouncer();
        AnimatedCharacter* MakeLooker();

        // ── 辅助：创建基础骨骼 ──
        std::shared_ptr<Skeleton> MakeHumanoid();
        std::shared_ptr<AnimationResource> MakeRes(std::shared_ptr<Skeleton> skel);

        // ── 输入 ──
        void HandleInput(float32 dt);

        // ── 更新 ──
        void UpdateChars(float32 dt);
        void UpdateDisplayTransforms();

        // ── 调试 ──
        void DrawUI();

        // ── 成员 ──
        IGraphicsFactory&       m_Factory;
        std::unique_ptr<IWindow> m_Window;
        InputManager            m_InputManager;
        TextureManager          m_TextureManager;
        SceneRenderer           m_SceneRenderer;
        PerspectiveCamera       m_Camera;

        std::shared_ptr<MeshRenderer> m_MeshRenderer;
        std::shared_ptr<Shader> m_3DShader;

        // 共享网格
        std::shared_ptr<Mesh> m_CubeMesh;
        std::shared_ptr<Mesh> m_SphereMesh;
        std::shared_ptr<Mesh> m_CylinderMesh;

        // 场景
        Scene m_Scene;

        // 角色列表
        std::vector<std::unique_ptr<AnimatedCharacter>> m_Characters;

        // 控制台 / 性能
        ConsolePanel      m_ConsolePanel;
        PerformanceWindow m_PerfWindow;
        MemoryPanel       m_MemoryPanel;
        bool m_UIInitialized = false;

        // 相机控制
        float m_CameraSpeed = 5.0f;
        float m_MouseSensitivity = 0.1f;
        bool m_MouseCaptured = false;

        // 调试开关
        bool m_PauseAnimation = false;
        float m_GlobalTimeScale = 1.0f;
    };

} // namespace Engine
