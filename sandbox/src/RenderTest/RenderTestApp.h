#pragma once

#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/Renderer/PerspectiveCamera.h>
#include <Engine/Core/RHI/MeshRenderer.h>
#include <Engine/Core/RHI/IPrimitiveBatch.h>
#include <Engine/Core/RHI/RHIBackend.h>
#include <Engine/Rendering/ShadowMapper.h>
#include <Engine/ConsoleLog.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/MeshComponent.h>
#include <Engine/Core/Renderer/Mesh.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/Log.h>
#include <Engine/PerformanceWindow.h>
#include <Engine/UIManager.h>
#include <Engine/MemoryTracker.h>
#include <Engine/Core/Physics/IPhysicsWorld3D.h>
#include <Engine/Core/Physics/IPhysicsBody3D.h>
#include <Engine/Core/Physics/PhysicsDefs3D.h>
#include <Engine/Jolt/JoltPhysicsWorld.h>
#include <Engine/OpenGL/OpenGLPhysicsDebugDraw3D.h>
#include <Engine/OpenGL/OpenGLContext.h>
#include <imgui.h>

#include <memory>
#include <vector>

namespace Engine {

// ── 真实场景物体类型 ──
enum class SceneObjectType : uint8 {
    Cube,
    Sphere,
    Cylinder,
    Pyramid,
    Torus
};

// 场景物体实例描述
struct SceneObjectDesc {
    SceneObjectType type = SceneObjectType::Cube;
    Vec3 position{0, 0, 0};
    Vec3 scale{1, 1, 1};
    Vec3 rotation{0, 0, 0};
    Vec4 color{1, 1, 1, 1};
};

class RenderTestApp {
public:
    RenderTestApp(IGraphicsFactory& factory);
    ~RenderTestApp();

    void Run();

private:
    IGraphicsFactory& m_Factory;
    std::unique_ptr<IWindow> m_Window;
    InputManager m_InputMgr;

    // ── 3D 场景 ──
    std::unique_ptr<Scene> m_Scene;
    std::unique_ptr<MeshRenderer> m_MeshRenderer;
    std::unique_ptr<PerspectiveCamera> m_Camera;
    std::shared_ptr<Shader> m_Shader3D;
    std::shared_ptr<Shader> m_DepthShader;
    std::unique_ptr<Rendering::ShadowMapper> m_ShadowMapper;

    std::vector<GameObject*> m_SceneObjects;
    GameObject* m_GroundPlane = nullptr;

    // ── 3D 物理（Jolt Physics） ──
    std::unique_ptr<JoltPhysicsWorld> m_PhysicsWorld;
    std::unique_ptr<OpenGLPhysicsDebugDraw3D> m_PhysicsDebugDraw;
    bool m_ShowCollisionShapes = false;
    float m_PhysicsAccumulator = 0.0f;
    static constexpr float kGravity = -30.0f;

    struct PhysicsBall {
        GameObject* gameObject;
        std::shared_ptr<IPhysicsBody3D> body;
        float restitution;
    };
    std::vector<PhysicsBall> m_PhysicsBalls;

    // ── UI ──
    PerformanceWindow m_PerfWindow;
    bool m_UIInitialized = false;

    // ── 调试状态 ──
    uint64 m_FrameCount = 0;
    float m_LightAngle = 0.0f;
    bool m_AnimateLight = true;
    bool m_ShowGrid = false;
    bool m_ShowAxis = false;

    RHI::Backend m_CurrentBackend = RHI::Backend::OpenGL46;

    // ── 物理抛射参数 ──
    float m_ThrowForce = 25.0f;
    float m_BallRestitutionMin = 0.1f;
    float m_BallRestitutionMax = 0.6f;  // 不超过 0.6 避免发散
    float m_SpawnRate = 0.0f;
    float m_SpawnCooldown = 0.0f;
    int m_SpawnCount = 0;
    static constexpr float kSphereRadius = 0.5f;

    // ── 阴影 ──
    bool m_ShadowsEnabled = true;
    float m_ShadowBias = 0.005f;

    // ── 相机控制 ──
    float m_CameraSpeed = 12.0f;
    float m_MouseSensitivity = 0.2f;
    bool m_InvertMouseY = false;
    bool m_RightDragging = false;
    double m_LastMouseX = 0.0, m_LastMouseY = 0.0;
    float m_StoredPitch = -25.0f;
    float m_StoredYaw = 0.0f;

    // ── GPU 信息 ──
    std::string m_GPUName;
    std::string m_BackendName = "OpenGL 4.6";
    std::string m_GLVersion, m_GLRenderer, m_GLVendor;

    bool InitUI();
    void BuildScene();
    void BuildGround();
    void InitPhysics();
    void SpawnPhysicsBall3D(const Vec3& origin, const Vec3& direction);
    void HandleInput(float dt);
    void DrawDebugPanel();
    void DrawGrid(float size, int steps);
    void DrawOriginAxis();
    void OnWindowResize(int w, int h);
    void QueryGPUInfo();
    void SyncCameraToPitchYaw();

    /** 生成金字塔网格 */
    static std::shared_ptr<Mesh> CreatePyramidMesh(float size = 1.0f);
    /** 生成圆环网格 */
    static std::shared_ptr<Mesh> CreateTorusMesh(float majorRadius = 1.0f, float minorRadius = 0.3f, int segments = 16);

    static const char* BackendToString(RHI::Backend b) {
        return RHI::BackendName(b);
    }
};

} // namespace Engine