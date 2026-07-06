/**
 * @file RenderTestApp.cpp
 * @brief 渲染画面验证 — 3D 场景 + Box2D物理 + ImGui 调试
 */

#include "RenderTestApp.h"
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <random>
#include <algorithm>

namespace Engine {

// ═══════════════════════════════════════════════════
// 工具函数
// ═══════════════════════════════════════════════════

static GameObject* MakeSceneObj(Scene& scene, const std::string& name,
                                 std::shared_ptr<Mesh> mesh, const Vec3& pos,
                                 const Vec4& color,
                                 const Vec3& scale = Vec3(1, 1, 1)) {
    auto obj = std::make_shared<GameObject>(name);
    obj->GetTransform().SetPosition(pos);
    obj->GetTransform().SetScale(scale);
    auto* mc = obj->AddComponent<MeshComponent>();
    mc->SetMesh(std::move(mesh));
    mc->m_Color = color;
    scene.AddObject(obj);
    return obj.get();
}

std::shared_ptr<Mesh> RenderTestApp::CreatePyramidMesh(float size) {
    float h = size * 0.5f;
    Vertex3D verts[18] = {
        {{-h, -h, -h}, {0,-1,0}, {0,0}, {0,0,0}},
        {{ h, -h, -h}, {0,-1,0}, {1,0}, {0,0,0}},
        {{ h, -h,  h}, {0,-1,0}, {1,1}, {0,0,0}},
        {{-h, -h, -h}, {0,-1,0}, {0,0}, {0,0,0}},
        {{ h, -h,  h}, {0,-1,0}, {1,1}, {0,0,0}},
        {{-h, -h,  h}, {0,-1,0}, {0,1}, {0,0,0}},
        {{ 0,  h,  0}, {0,0.894,0.447}, {0.5,1}, {0,0,0}},
        {{-h, -h,  h}, {0,0.894,0.447}, {0,0}, {0,0,0}},
        {{ h, -h,  h}, {0,0.894,0.447}, {1,0}, {0,0,0}},
        {{ 0,  h,  0}, {0.894,0.447,0}, {0.5,1}, {0,0,0}},
        {{ h, -h,  h}, {0.894,0.447,0}, {0,0}, {0,0,0}},
        {{ h, -h, -h}, {0.894,0.447,0}, {1,0}, {0,0,0}},
        {{ 0,  h,  0}, {0,-0.894,-0.447}, {0.5,1}, {0,0,0}},
        {{ h, -h, -h}, {0,-0.894,-0.447}, {0,0}, {0,0,0}},
        {{-h, -h, -h}, {0,-0.894,-0.447}, {1,0}, {0,0,0}},
        {{ 0,  h,  0}, {-0.894,0.447,0}, {0.5,1}, {0,0,0}},
        {{-h, -h, -h}, {-0.894,0.447,0}, {0,0}, {0,0,0}},
        {{-h, -h,  h}, {-0.894,0.447,0}, {1,0}, {0,0,0}},
    };
    std::vector<Vertex3D> vertVec(verts, verts + 18);
    std::vector<uint32> idx = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17};
    return std::make_shared<Mesh>(vertVec, idx);
}

std::shared_ptr<Mesh> RenderTestApp::CreateTorusMesh(float majorR, float minorR, int segs) {
    std::vector<Vertex3D> verts;
    std::vector<uint32> idx;
    int slices = segs;
    int rings = segs / 2;
    if (rings < 4) rings = 4;
    for (int j = 0; j <= rings; ++j) {
        float phi = (float)j / rings * 6.2832f;
        for (int i = 0; i <= slices; ++i) {
            float theta = (float)i / slices * 6.2832f;
            float x = (majorR + minorR * std::cos(theta)) * std::cos(phi);
            float y = minorR * std::sin(theta);
            float z = (majorR + minorR * std::cos(theta)) * std::sin(phi);
            Vec3 norm(x - majorR * std::cos(phi), y, z - majorR * std::sin(phi));
            float len = std::sqrt(norm.x*norm.x + norm.y*norm.y + norm.z*norm.z);
            if (len > 0) { norm.x/=len; norm.y/=len; norm.z/=len; }
            verts.push_back({Vec3(x,y,z), norm, Vec2((float)i/slices,(float)j/rings), {0,0,0}});
        }
    }
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < slices; ++i) {
            int a = j*(slices+1)+i, b = a+slices+1;
            idx.push_back(a); idx.push_back(b); idx.push_back(a+1);
            idx.push_back(a+1); idx.push_back(b); idx.push_back(b+1);
        }
    return std::make_shared<Mesh>(verts, idx);
}

// ═══════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════

RenderTestApp::RenderTestApp(IGraphicsFactory& factory)
    : m_Factory(factory), m_PerfWindow() {}

RenderTestApp::~RenderTestApp() {
    if (m_UIInitialized) { UIManager::Shutdown(); m_UIInitialized = false; }
}

bool RenderTestApp::InitUI() {
    auto ui = m_Factory.CreateUIManager();
    if (!ui) return false;
    m_UIInitialized = UIManager::Init(std::move(ui),
        m_Window->GetNativeHandle(), m_Window->GetContext());
    return m_UIInitialized;
}

void RenderTestApp::QueryGPUInfo() {
    auto* ctx = m_Window->GetContext();
    if (auto* oglCtx = dynamic_cast<OpenGLContext*>(ctx)) {
        auto& gl = oglCtx->GetGL();
        const char* v = (const char*)gl.GetString(GL_VERSION);
        const char* r = (const char*)gl.GetString(GL_RENDERER);
        const char* ve = (const char*)gl.GetString(GL_VENDOR);
        if (v) m_GLVersion = v;
        if (r) m_GLRenderer = r;
        if (ve) m_GLVendor = ve;
        m_GPUName = m_GLRenderer;
    }
    if (m_GPUName.empty()) m_GPUName = "Unknown GPU";
}

void RenderTestApp::SyncCameraToPitchYaw() {
    if (m_Camera) m_Camera->SetRotation(m_StoredPitch, m_StoredYaw);
}

// ═══════════════════════════════════════════════════
// 场景构建
// ═══════════════════════════════════════════════════

void RenderTestApp::BuildGround() {
    auto groundMesh = std::make_shared<Mesh>(Mesh::CreatePlane(1000, 1000));
    auto ground = std::make_shared<GameObject>("Ground");
    ground->GetTransform().SetPosition(Vec3(0, -2.5f, 0));
    auto* gmc = ground->AddComponent<MeshComponent>();
    gmc->SetMesh(groundMesh);
    gmc->m_Color = Vec4(0.18f, 0.22f, 0.18f, 1.0f);
    m_Scene->AddObject(ground);
    m_SceneObjects.push_back(ground.get());
    m_GroundPlane = ground.get();
}

void RenderTestApp::BuildScene() {
    m_Scene = std::make_unique<Scene>("RenderTest Scene");
    std::mt19937 rng(42);
    auto rnd = [&](float lo, float hi) { return lo + (hi-lo)*((float)rng()/(float)rng.max()); };

    BuildGround();

    auto sphereM = std::make_shared<Mesh>(Mesh::CreateSphere(1.0f, 24));
    auto cubeM = std::make_shared<Mesh>(Mesh::CreateCube(1.2f));
    auto cylM = std::make_shared<Mesh>(Mesh::CreateCylinder(0.8f, 2.0f, 16));
    auto pyramidM = CreatePyramidMesh(1.2f);
    auto torusM = CreateTorusMesh(1.2f, 0.3f, 16);

    // 5 种不同物体在中心环
    struct { std::shared_ptr<Mesh> mesh; Vec4 color; const char* label; } objs[] = {
        {sphereM, Vec4(0.2f,0.5f,1.0f,1), "BlueSphere"},
        {cubeM, Vec4(1.0f,0.2f,0.2f,1), "RedCube"},
        {cylM, Vec4(0.2f,1.0f,0.5f,1), "GreenCyl"},
        {pyramidM, Vec4(1.0f,0.8f,0.2f,1), "GoldPyramid"},
        {torusM, Vec4(0.8f,0.3f,1.0f,1), "PurpleTorus"},
    };
    for (int i = 0; i < 5; ++i) {
        float a = (float)i/5*6.2832f;
        auto* obj = MakeSceneObj(*m_Scene, objs[i].label, objs[i].mesh,
            Vec3(std::cos(a)*4, 0.5f, std::sin(a)*4), objs[i].color);
        obj->GetTransform().SetRotation(0, a*57.3f, 0);
        m_SceneObjects.push_back(obj);
    }

    // 外圈 2 圈混合物体
    for (int ring = 0; ring < 2; ++ring) {
        float rad = 8.0f + ring*3.0f;
        int n = 8 + ring*4;
        for (int i = 0; i < n; ++i) {
            float a = (float)i/n*6.2832f + ring*0.5f;
            int ti = (int)rnd(0, 4.99f);
            std::shared_ptr<Mesh> selM = ti==0?sphereM:ti==1?cubeM:ti==2?cylM:ti==3?pyramidM:torusM;
            Vec4 c(rnd(0.3f,1), rnd(0.3f,1), rnd(0.3f,1), 1);
            auto* obj = MakeSceneObj(*m_Scene, "Obj"+std::to_string(i), selM,
                Vec3(std::cos(a)*rad, 0.0f, std::sin(a)*rad), c, Vec3(rnd(0.6f,1.2f), rnd(0.6f,1.2f), rnd(0.6f,1.2f)));
            obj->GetTransform().SetRotation(rnd(0,360), rnd(0,360), 0);
            m_SceneObjects.push_back(obj);
        }
    }
}

// ═══════════════════════════════════════════════════
// 3D 物理系统（Jolt Physics）
// ═══════════════════════════════════════════════════

void RenderTestApp::InitPhysics() {
    // Jolt Physics 世界配置
    PhysicsWorldConfig3D config;
    config.gravity = Vec3(0.0f, -30.0f, 0.0f);  // Y轴向下重力
    config.maxBodies = 65536;
    config.maxContactConstraints = 10240;
    config.maxPairs = 65536;
    config.velocitySteps = 10;
    config.positionSteps = 2;

    m_PhysicsWorld = std::make_unique<JoltPhysicsWorld>();
    m_PhysicsWorld->Init(config);

    // 创建地面碰撞体（无限大平面）
    BodyDef3D groundDef;
    groundDef.type = BodyType3D::Static;
    groundDef.shape.type = ShapeType3D::Plane;
    groundDef.shape.planeNormal = Vec3(0.0f, 1.0f, 0.0f);
    groundDef.shape.planeDistance = 0.0f;  // 地面在 y=0 平面
    groundDef.friction = 0.8f;
    groundDef.restitution = 0.05f;
    m_PhysicsWorld->CreateBody(groundDef);

    // 调试绘制
    auto* ctx = m_Window->GetContext();
    if (auto* oglCtx = dynamic_cast<OpenGLContext*>(ctx)) {
        m_PhysicsDebugDraw = std::make_unique<OpenGLPhysicsDebugDraw3D>(oglCtx->GetGL());
        m_PhysicsWorld->SetDebugDraw(m_PhysicsDebugDraw.get());
    }
}

void RenderTestApp::SpawnPhysicsBall3D(const Vec3& origin, const Vec3& direction) {
    if (!m_PhysicsWorld || !m_MeshRenderer) return;

    std::mt19937 rng((uint32)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    auto hueDist = std::uniform_real_distribution<float>(0, 1);
    float hue = hueDist(rng);
    Vec4 col(0.5f+0.5f*std::cos(hue*6.2832f), 0.5f+0.5f*std::cos((hue+0.333f)*6.2832f),
             0.5f+0.5f*std::cos((hue+0.667f)*6.2832f), 1);

    auto sphereMesh = std::make_shared<Mesh>(Mesh::CreateSphere(kSphereRadius, 12));
    auto* obj = MakeSceneObj(*m_Scene, "PhysBall"+std::to_string(m_SpawnCount), sphereMesh, origin, col);

    // 随机弹性 [0.1, 0.6]
    float restitute = 0.1f + hueDist(rng)*0.5f;

    // 3D 物理体定义：球体形状
    BodyDef3D bodyDef;
    bodyDef.type = BodyType3D::Dynamic;
    bodyDef.position = origin;  // 3D 世界位置
    bodyDef.shape.type = ShapeType3D::Sphere;
    bodyDef.shape.sphereRadius = kSphereRadius;
    bodyDef.density = 1.5f;
    bodyDef.friction = 0.4f;
    bodyDef.restitution = restitute;
    bodyDef.linearDamping = 0.1f;
    bodyDef.angularDamping = 0.1f;

    // 设置初始速度（冲量方向）
    bodyDef.initialLinearVelocity = Vec3(
        direction.x * m_ThrowForce * 0.5f,
        direction.y * m_ThrowForce * 0.5f,
        direction.z * m_ThrowForce * 0.5f);

    auto body = m_PhysicsWorld->CreateBody(bodyDef);

    m_PhysicsBalls.push_back({obj, body, restitute});
    m_SceneObjects.push_back(obj);
    m_SpawnCount++;
}

// ═══════════════════════════════════════════════════
// 主循环
// ═══════════════════════════════════════════════════

void RenderTestApp::Run() {
    m_Window = m_Factory.CreateWindow(1280, 720, "RenderTest - 3D Scene + Physics");
    if (!m_Window) { LOG_ERROR("Failed to create window"); return; }

    auto* glfwWin = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());
    auto* ctx = m_Window->GetContext();
    if (!ctx) { LOG_ERROR("Failed to get render context"); return; }
    m_InputMgr.Init(m_Window.get());

    QueryGPUInfo();

    // 相机
    m_Camera = std::make_unique<PerspectiveCamera>(60.0f, 1280.0f/720.0f, 0.1f, 500.0f);
    m_Camera->SetPosition(Vec3(0, 10, 25));
    m_StoredPitch = -20.0f; m_StoredYaw = 0.0f;
    SyncCameraToPitchYaw();

    // MeshRenderer
    m_MeshRenderer = std::make_unique<MeshRenderer>(m_Factory, *ctx);
    m_MeshRenderer->SetCamera(m_Camera.get());
    m_MeshRenderer->SetAmbientColor(Vec3(0.55f, 0.58f, 0.62f));
    m_MeshRenderer->SetShadowEnabled(true);

    // 着色器
    m_Shader3D = m_Factory.CreateShader("assets/shaders/3d_lit.vert", "assets/shaders/3d_lit.frag");
    if (!m_Shader3D) m_Shader3D = m_Factory.CreateShader("assets/shaders/simple.vert", "assets/shaders/simple.frag");
    m_MeshRenderer->SetShader(m_Shader3D);

    m_DepthShader = m_Factory.CreateShader("assets/shaders/depth_only.vert", "assets/shaders/depth_only.frag");
    if (m_DepthShader) m_MeshRenderer->SetDepthShader(m_DepthShader);

    // 光源
    m_MeshRenderer->AddLight({{20,30,20}, {1,0.95f,0.9f}, 2.0f});
    m_MeshRenderer->AddLight({{-15,12,18}, {0.9f,0.7f,1}, 1.2f});
    m_MeshRenderer->AddLight({{10,8,-20}, {1,0.5f,0.3f}, 0.8f});

    BuildScene();
    std::printf("  [RenderTest] Scene built with %zu objects\n", m_SceneObjects.size());

    InitPhysics();
    InitUI();
    m_PerfWindow.SetRenderContext(ctx);
    m_PerfWindow.SetVisible(true);

    glfwShowWindow(glfwWin);
    glfwFocusWindow(glfwWin);

    int initW, initH;
    glfwGetFramebufferSize(glfwWin, &initW, &initH);
    if (initW > 0 && initH > 0) OnWindowResize(initW, initH);

    auto lastTime = std::chrono::high_resolution_clock::now();

    // GPU Pass 查询索引
    int32 passIdx_Scene = -1;
    int32 passIdx_Aux   = -1;
    int32 passIdx_UI    = -1;

    while (!m_Window->ShouldClose()) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::min(std::chrono::duration<float>(now - lastTime).count(), 0.05f);
        lastTime = now;

        m_Window->PollEvents();
        HandleInput(dt);
        MemoryTracker::FrameStart();

        int w, h;
        glfwGetFramebufferSize(glfwWin, &w, &h);
        bool minimized = (w == 0 || h == 0);
        static int lw=0, lh=0;
        if (w != lw || h != lh) { lw=w; lh=h; if (!minimized) OnWindowResize(w, h); }
        ++m_FrameCount;
        if (minimized) { MemoryTracker::FrameEnd(); m_InputMgr.OnUpdate(); continue; }

        // ── 3D 物理更新 60Hz ──
        m_PhysicsAccumulator += dt;
        while (m_PhysicsAccumulator >= 1.0f/60.0f) {
            m_PhysicsAccumulator -= 1.0f/60.0f;

            // 更新物理球体位置（Jolt 3D API — GetPosition 返回 Vec3）
            for (auto& pb : m_PhysicsBalls) {
                if (pb.body && pb.gameObject) {
                    Vec3 pos = pb.body->GetPosition();
                    // Jolt 3D 位置已在世界空间，直接使用
                    pb.gameObject->GetTransform().SetPosition(pos);
                }
            }

            // 调试绘制：清除并准备绘制碰撞形状
            if (m_ShowCollisionShapes && m_PhysicsDebugDraw) {
                m_PhysicsDebugDraw->Clear();
                m_PhysicsWorld->DebugDraw();
                m_PhysicsDebugDraw->Flush();
            }

            m_PhysicsWorld->Step(1.0f/60.0f, 1);
        }

        // ── 光源动画 ──
        if (m_AnimateLight) {
            m_LightAngle += dt * 20.0f;
            float rad = m_LightAngle * 3.14159f / 180.0f;
            m_MeshRenderer->SetLightPosition(Vec3(std::cos(rad)*30, 25+std::sin(rad*0.5f)*5, std::sin(rad)*30));
        }

        auto* oglCtx = dynamic_cast<OpenGLContext*>(ctx);
        if (oglCtx) {
            oglCtx->GetGL().Enable(GL_DEPTH_TEST);
            oglCtx->GetGL().DepthFunc(GL_LESS);
            oglCtx->GetGL().DepthMask(GL_TRUE);
            oglCtx->GetGL().Disable(GL_BLEND);
            oglCtx->GetGL().Disable(GL_CULL_FACE);
        }

        // ═══════════════════════════════════════════════
        // GPU Pass 1: 主场景渲染 (不透明)
        // ═══════════════════════════════════════════════
        passIdx_Scene = ctx->BeginGPUPass("Scene_Render");
        ctx->ClearColor(0.08f, 0.12f, 0.22f, 1.0f);

        // 阴影 Pass
        if (m_ShadowsEnabled && m_MeshRenderer->IsShadowEnabled()) {
            m_MeshRenderer->RenderShadowPass(m_SceneObjects);
        }

        if (m_Shader3D) {
            m_Shader3D->Bind();
            if (oglCtx) oglCtx->BindViewModeUniform(m_Shader3D.get());
        }
        m_MeshRenderer->Render(m_SceneObjects);
        if (passIdx_Scene >= 0) ctx->EndGPUPass(passIdx_Scene);

        // ═══════════════════════════════════════════════
        // GPU Pass 2: 辅助线 + 碰撞形状 (半透明)
        // ═══════════════════════════════════════════════
        passIdx_Aux = ctx->BeginGPUPass("Grid_Physics");

        if (oglCtx) {
            oglCtx->GetGL().Enable(GL_BLEND);
            oglCtx->GetGL().BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        if (m_Shader3D) {
            m_Shader3D->Bind();
            const Mat4& proj = m_Camera->GetProjectionMatrix();
            const Mat4& view = m_Camera->GetViewMatrix();
            m_Shader3D->SetMat4("u_View", view.Data());
            m_Shader3D->SetMat4("u_Projection", proj.Data());
            m_Shader3D->SetMat4("u_Model", Mat4().Data());
            glm::mat4 pv = glm::make_mat4(proj.Data()) * glm::make_mat4(view.Data());
            float mvpData[16];
            std::memcpy(mvpData, &pv, sizeof(float)*16);
            m_Shader3D->SetMat4("u_MVP", mvpData);
            m_Shader3D->SetInt("u_LightCount", 0);
            Vec3 white(1,1,1);
            m_Shader3D->SetVec3("u_AmbientColor", &white.x);
        }

        // 网格
        auto* batch = m_MeshRenderer->GetBatch();
        if (batch) {
            if (m_ShowGrid) DrawGrid(25.0f, 50);
            if (m_ShowAxis) DrawOriginAxis();
        }

        // ── 碰撞形状调试：需设置 ViewProjection 矩阵 ──
        // ── 碰撞形状调试：设置 VP 矩阵 ──
        if (m_ShowCollisionShapes && m_PhysicsWorld && m_PhysicsDebugDraw) {
            Mat4 vp;
            const Mat4& proj = m_Camera->GetProjectionMatrix();
            const Mat4& view = m_Camera->GetViewMatrix();
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col) {
                    float sum = 0;
                    for (int k = 0; k < 4; ++k)
                        sum += proj.data[row + k*4] * view.data[k + col*4];
                    vp.data[row + col*4] = sum;
                }
            m_PhysicsDebugDraw->SetViewProjection(vp.Data());
            // 物理更新中已调用了 Clear/DebugDraw/Flush，这里不重复调用
        }

        if (passIdx_Aux >= 0) ctx->EndGPUPass(passIdx_Aux);

        // ═══════════════════════════════════════════════
        // GPU Pass 3: UI 覆盖层
        // ═══════════════════════════════════════════════
        passIdx_UI = ctx->BeginGPUPass("UI_Overlay");
        if (oglCtx) oglCtx->GetGL().Clear(GL_DEPTH_BUFFER_BIT);

        if (m_UIInitialized && UIManager::Get()) {
            if (oglCtx) oglCtx->ResetPipelineState();

            uint32 dc = ctx->GetAndResetDrawCallCount();
            uint32 vc = ctx->GetAndResetVertexCount();
            uint32 tc = ctx->GetAndResetTriangleCount();
            m_PerfWindow.FeedStats(dt*1000, dc, (uint32)m_SceneObjects.size(), 0, 0, 0);
            m_PerfWindow.SetGeometryCount(vc, tc);
            m_PerfWindow.SetCPUTime(dt*1000);

            GPUProfileFrame gf;
            if (ctx->GetGPUProfileFrame(gf) && gf.passCount > 0) {
                GPUProfilerSnapshot snap;
                snap.passCount = std::min(gf.passCount, (uint32)GPUProfilerSnapshot::kMaxPasses);
                for (uint32 i = 0; i < snap.passCount; ++i) {
                    snap.passes[i].name = gf.passes[i].passName;
                    snap.passes[i].timeMs = gf.passes[i].elapsedMs;
                }
                m_PerfWindow.SetGPUProfiler(snap);
            }

            UIManager::Begin();
            DrawDebugPanel();
            m_PerfWindow.OnImGui();
            UIManager::End();
        }
        if (passIdx_UI >= 0) ctx->EndGPUPass(passIdx_UI);

        MemoryTracker::FrameEnd();
        Input::SetBlockInput(false, false);
        m_InputMgr.OnUpdate();
        ctx->SwapBuffers();
    }
}

// ═══════════════════════════════════════════════════
// 输入处理
// ═══════════════════════════════════════════════════

void RenderTestApp::HandleInput(float dt) {
    auto* gw = static_cast<GLFWwindow*>(m_Window->GetNativeHandle());

    static bool wasF3 = false;
    bool isF3 = glfwGetKey(gw, GLFW_KEY_F3) == GLFW_PRESS;
    if (isF3 && !wasF3) m_PerfWindow.ToggleVisibility();
    wasF3 = isF3;

    // Space: 从摄像机发射球体
    static bool wasSpace = false;
    bool isSpace = glfwGetKey(gw, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (isSpace && !wasSpace) {
        Vec3 pos = m_Camera->GetPosition();
        Vec3 fwd = m_Camera->GetForward();
        // 出生点沿着视线方向偏移 2 单位
        // 高度限制: 不低于地面 (-2.5) + 半径 (0.5) = -2.0
        Vec3 spawnPos = pos + fwd * 2.0f;
        if (spawnPos.y < -1.5f) spawnPos.y = -1.5f;
        SpawnPhysicsBall3D(spawnPos, fwd);
    }
    wasSpace = isSpace;

    if (glfwGetKey(gw, GLFW_KEY_ESCAPE) == GLFW_PRESS) { m_Window->SetShouldClose(true); return; }

    bool wantKB = UIManager::WantCaptureKeyboard();
    bool rmb = glfwGetMouseButton(gw, GLFW_MOUSE_BUTTON_2) == GLFW_PRESS;

    // WASD
    if (!wantKB && m_Camera) {
        Vec3 pos = m_Camera->GetPosition(), fwd = m_Camera->GetForward(), rgt = m_Camera->GetRight();
        float s = m_CameraSpeed * dt;
        if (glfwGetKey(gw, GLFW_KEY_W) == GLFW_PRESS) pos = pos + fwd*s;
        if (glfwGetKey(gw, GLFW_KEY_S) == GLFW_PRESS) pos = pos - fwd*s;
        if (glfwGetKey(gw, GLFW_KEY_A) == GLFW_PRESS) pos = pos - rgt*s;
        if (glfwGetKey(gw, GLFW_KEY_D) == GLFW_PRESS) pos = pos + rgt*s;
        if (glfwGetKey(gw, GLFW_KEY_Q) == GLFW_PRESS) pos.y -= s;
        if (glfwGetKey(gw, GLFW_KEY_E) == GLFW_PRESS) pos.y += s;
        m_Camera->SetPosition(pos);
    }

    // 右键拖拽旋转
    if (rmb && m_Camera) {
        double mx, my;
        glfwGetCursorPos(gw, &mx, &my);
        if (!m_RightDragging) {
            m_LastMouseX = mx; m_LastMouseY = my;
            m_StoredPitch = m_Camera->GetPitch();
            m_StoredYaw = m_Camera->GetYaw();
            m_RightDragging = true;
        } else {
            double dx = mx - m_LastMouseX, dy = my - m_LastMouseY;
            m_LastMouseX = mx; m_LastMouseY = my;
            float sens = m_MouseSensitivity;
            m_StoredYaw += (float)dx * sens;
            float pitchDelta = (float)dy * sens * (m_InvertMouseY ? -1.0f : 1.0f);
            m_StoredPitch = std::max(-89.0f, std::min(89.0f, m_StoredPitch - pitchDelta));
            SyncCameraToPitchYaw();
        }
    } else {
        m_RightDragging = false;
    }
}

// ═══════════════════════════════════════════════════
// 调试面板
// ═══════════════════════════════════════════════════

void RenderTestApp::DrawDebugPanel() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("RenderTest Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextColored(ImVec4(0.3f,0.8f,1,1), "RenderTest - Render Verification");
    ImGui::Separator();

    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Frame: %llu", (unsigned long long)m_FrameCount);
    ImGui::Text("Objects: %zu", m_SceneObjects.size());

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.3f,0.8f,1,1), "Rendering");
    const char* backendItems[] = {"OpenGL 4.6", "Vulkan 1.3", "D3D12"};
    int bi = (int)m_CurrentBackend;
    if (ImGui::Combo("Backend", &bi, backendItems, IM_ARRAYSIZE(backendItems))) m_CurrentBackend = (RHI::Backend)bi;
    ImGui::Text("GPU: %s", m_GPUName.c_str());

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f,0.5f,1,1), "Shadows");
    ImGui::Checkbox("Enable Shadows", &m_ShadowsEnabled);
    if (m_MeshRenderer) m_MeshRenderer->SetShadowEnabled(m_ShadowsEnabled);

    auto* ctx = m_Window->GetContext();
    if (ctx) { ImGui::Text("Draw Calls: %u", ctx->GetAndResetDrawCallCount()); }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.3f,1,0.8f,1), "Camera");
    if (m_Camera) {
        Vec3 p = m_Camera->GetPosition();
        ImGui::Text("Pos: (%.1f, %.1f, %.1f)", p.x, p.y, p.z);
        ImGui::Text("Pitch: %.1f  Yaw: %.1f", m_Camera->GetPitch(), m_Camera->GetYaw());
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1,0.5f,0.5f,1), "Mouse Control");
    ImGui::Checkbox("Invert Y", &m_InvertMouseY);
    ImGui::SliderFloat("Sensitivity", &m_MouseSensitivity, 0.05f, 1.0f, "%.2f");
    ImGui::SliderFloat("Move Speed", &m_CameraSpeed, 1, 50, "%.0f");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "Lighting");
    ImGui::Checkbox("Animate Light", &m_AnimateLight);
    if (!m_AnimateLight) ImGui::SliderFloat("Angle", &m_LightAngle, 0, 360);
    Vec3 amb = m_MeshRenderer ? m_MeshRenderer->GetAmbientColor() : Vec3(0.55f,0.58f,0.62f);
    if (ImGui::SliderFloat("Ambient", &amb.x, 0, 1, "%.2f")) { amb.y=amb.z=amb.x; if (m_MeshRenderer) m_MeshRenderer->SetAmbientColor(amb); }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f,0.8f,1,1), "Helpers");
    ImGui::Checkbox("Grid", &m_ShowGrid);
    ImGui::Checkbox("Axis", &m_ShowAxis);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.3f,1,0.5f,1), "Physics");
    ImGui::Checkbox("Show Collision Shapes", &m_ShowCollisionShapes);
    ImGui::SliderFloat("Throw Force", &m_ThrowForce, 5, 60, "%.0f");
    ImGui::Text("Balls: %zu", m_PhysicsBalls.size());
    if (ImGui::Button("Spawn Ball")) {
        Vec3 pos = m_Camera->GetPosition();
        Vec3 fwd = m_Camera->GetForward();
        Vec3 spawnPos = pos + fwd * 2.0f;
        if (spawnPos.y < -1.5f) spawnPos.y = -1.5f;
        SpawnPhysicsBall3D(spawnPos, fwd);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
        for (auto& pb : m_PhysicsBalls) {
            if (pb.body) m_PhysicsWorld->DestroyBody(pb.body.get());
            auto it = std::find(m_SceneObjects.begin(), m_SceneObjects.end(), pb.gameObject);
            if (it != m_SceneObjects.end()) m_SceneObjects.erase(it);
        }
        m_PhysicsBalls.clear(); m_SpawnCount = 0;
    }
    ImGui::Text("Space = Launch ball | RMB+Drag = Look");

    ImGui::End();
}

// ═══════════════════════════════════════════════════
// 辅助绘制
// ═══════════════════════════════════════════════════

void RenderTestApp::DrawGrid(float size, int steps) {
    auto* batch = m_MeshRenderer ? m_MeshRenderer->GetBatch() : nullptr;
    if (!batch) return;
    batch->Begin(PrimitiveType::Lines);
    float half = size;
    float step = size*2.0f/steps;
    Vec4 gray(0.4f,0.4f,0.4f,0.45f), red(1,0.2f,0.2f,0.7f), blue(0.2f,0.2f,1,0.7f);
    for (int i = 0; i <= steps; ++i) {
        float t = -half + i*step;
        batch->Line(Vec3(-half, -2.49f, t), Vec3(half, -2.49f, t), (i==steps/2)?red:gray);
        batch->Line(Vec3(t, -2.49f, -half), Vec3(t, -2.49f, half), (i==steps/2)?blue:gray);
    }
    batch->End();
}

void RenderTestApp::DrawOriginAxis() {
    auto* batch = m_MeshRenderer ? m_MeshRenderer->GetBatch() : nullptr;
    if (!batch) return;
    batch->Begin(PrimitiveType::Lines);
    batch->Line(Vec3(0,0,0), Vec3(3,0,0), Vec4(1,0,0,1));
    batch->Line(Vec3(0,0,0), Vec3(0,3,0), Vec4(0,1,0,1));
    batch->Line(Vec3(0,0,0), Vec3(0,0,3), Vec4(0,0,1,1));
    batch->End();
}

void RenderTestApp::OnWindowResize(int w, int h) {
    if (m_Camera) m_Camera->SetAspectRatio((float)w/(float)h);
    if (auto* c = m_Window->GetContext()) c->OnResize(w, h);
}

} // namespace Engine