/**
 * @file VisualRenderDemo.cpp
 * @brief 可视化 3D 渲染演示 — 窗口 + 多后端热切换 + GPU Pass 测试
 *
 * 重要：OpenGLGraphicsFactory 必须在任何 windows.h 之前包含，
 * 否则 Windows 的 CreateWindow 宏会破坏工厂方法签名。
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define VK_NO_PROTOTYPES

#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include <Engine/OpenGL/OpenGLContext.h>
#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/IWindow.h>
#include <Engine/Core/IRenderContext.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/Renderer/PerspectiveCamera.h>
#include <Engine/Core/Renderer/Mesh.h>
#include <Engine/Core/RHI/MeshRenderer.h>
#include <Engine/Core/RenderResources/Shader.h>
#include <Engine/Core/GameObject/MeshComponent.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/RHI/RHIBackend.h>
#include <Engine/Core/RHI/RHIWindow.h>
#include <Engine/Core/RHI/IPrimitiveBatch.h>

#include "Rendering3DTest.h"

#ifdef CreateWindow
#undef CreateWindow
#endif

#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <random>
#include <vector>
#include <memory>

using std::max;
using std::min;

namespace Engine {

struct VisualRenderDemo {
    // ── 多后端支持 ──
    std::unique_ptr<OpenGLGraphicsFactory> glFactory;
    std::unique_ptr<IGraphicsFactory> activeFactory;
    std::unique_ptr<IWindow> window;
    RHI::RHIBackend currentBackend = RHI::RHIBackend::OpenGL;
    int fbW = 1280, fbH = 720;
    bool backendDirty = false;

    // ── 渲染核心 ──
    std::unique_ptr<MeshRenderer> meshRenderer;
    PerspectiveCamera camera{60.0f, 1280.0f / 720.0f, 0.1f, 200.0f};
    std::shared_ptr<Shader> shader3D;
    InputManager inputMgr;
    Logger log = Logger("VisualDemo");

    // ── 场景 ──
    Scene scene{"3D Scene"};
    std::vector<GameObject*> sceneObjects;
    GladGLContext* gl = nullptr;

    // ── 统计 ──
    uint64 frameCount = 0;
    float lightAngle = 0.0f;
    double ecsCreateTime_ms = 0.0;
    uint32 entityCount = 0;

    // ── GPU Pass 计时 ──
    static constexpr int kNumGPUPasses = 4;
    std::string passNames[kNumGPUPasses] = {
        "Scene_Render",
        "Grid_Axis",
        "State_Reset",
        "UI_Swap"
    };
    int32 passQueryIndices[kNumGPUPasses] = {-1, -1, -1, -1};

    // ── 后端名称映射 ──
    static const char* BackendToName(RHI::RHIBackend backend) {
        switch (backend) {
            case RHI::RHIBackend::OpenGL: return "OpenGL 4.6";
            case RHI::RHIBackend::Vulkan: return "Vulkan";
            case RHI::RHIBackend::D3D12:  return "D3D12";
            default: return "Unknown";
        }
    }

    bool Initialize() {
        // 始终创建 OpenGL 工厂（作为默认/备用后端）
        glFactory = std::make_unique<OpenGLGraphicsFactory>();
        activeFactory = std::make_unique<OpenGLGraphicsFactory>();

        window = activeFactory->CreateWindow(1280, 720, "R3DTest - Visual 3D Demo");
        if (!window) return false;

        auto* glfwWin = static_cast<GLFWwindow*>(window->GetNativeHandle());
        glfwShowWindow(glfwWin);
        inputMgr.Init(window.get());

        // 获取原始 GL 上下文指针
        auto* oglCtx = static_cast<OpenGLContext*>(window->GetContext());
        if (oglCtx) {
            gl = &oglCtx->GetGL();
        }

        // 相机位置 — 远离原点，让所有球体在视野前方
        camera.SetPosition(Vec3(0, 8, 28));
        camera.SetRotation(-15, 0);
        camera.SetAspectRatio(1280.0f / 720.0f);

        auto* ctx = window->GetContext();
        meshRenderer = std::make_unique<MeshRenderer>(*activeFactory, *ctx);
        meshRenderer->SetCamera(&camera);

        // ── 着色器加载 ──
        shader3D = activeFactory->CreateShader("assets/shaders/3d_lit.vert", "assets/shaders/3d_lit.frag");
        if (!shader3D) {
            log.Warn("3d_lit shader not found, trying simple.vert + simple.frag");
            shader3D = activeFactory->CreateShader("assets/shaders/simple.vert", "assets/shaders/simple.frag");
        }
        if (!shader3D) {
            log.Error("CRITICAL: No shader loaded!");
            return false;
        }
        meshRenderer->SetShader(shader3D);

        auto depthShader = activeFactory->CreateShader("assets/shaders/depth_only.vert", "assets/shaders/depth_only.frag");
        if (depthShader) meshRenderer->SetDepthShader(depthShader);

        // 光照 — 增强环境光 + 多光源
        meshRenderer->SetAmbientColor(Vec3(0.6f, 0.65f, 0.7f));  // 提高环境光避免太暗
        meshRenderer->AddLight({{15, 20, 15}, {1, 1, 1}, 1.5f});
        meshRenderer->AddLight({{-10, 8, 12}, {0.8f, 0.6f, 1.0f}, 0.8f});
        meshRenderer->AddLight({{5, 3, -12}, {1.0f, 0.4f, 0.2f}, 0.6f});

        BuildScene();
        return true;
    }

    void BuildScene() {
        auto startT = std::chrono::high_resolution_clock::now();

        // ── 地面 ──
        auto groundMesh = std::make_shared<Mesh>(Mesh::CreatePlane(80, 80));
        auto ground = std::make_shared<GameObject>("Ground");
        ground->GetTransform().SetPosition(Vec3(0, -3, 0));
        auto* gmc = ground->AddComponent<MeshComponent>();
        gmc->SetMesh(groundMesh);
        gmc->m_Color = Vec4(0.3f, 0.35f, 0.3f, 1.0f);
        scene.AddObject(ground);
        sceneObjects.push_back(ground.get());

        // ── 10,000 彩色球体 — 分布在远离相机的位置 ──
        // 球体分布在半径 8~30 的球壳上，确保不会出现在相机正前方近距离
        std::mt19937 rng(42);
        auto rnd = [&](float lo, float hi) {
            return lo + (hi - lo) * ((float)rng() / (float)rng.max());
        };
        auto sphereM = std::make_shared<Mesh>(Mesh::CreateSphere(0.35f, 8));

        constexpr uint32 N = 10000;
        for (uint32 i = 0; i < N; ++i) {
            float theta = rnd(0, 6.2832f);
            float phi = std::acos(rnd(-1.0f, 1.0f));
            float radius = rnd(8.0f, 30.0f);  // 最小半径 8，避免遮挡视野

            float x = std::sin(phi) * std::cos(theta) * radius;
            float z = std::sin(phi) * std::sin(theta) * radius;
            float y = std::cos(phi) * radius * 0.6f;
            y = std::max(-10.0f, std::min(15.0f, y));

            auto obj = std::make_shared<GameObject>("S" + std::to_string(i));
            obj->GetTransform().SetPosition(Vec3(x, y, z));
            float s = rnd(0.3f, 1.0f);
            obj->GetTransform().SetScale(Vec3(s, s, s));

            auto* mc = obj->AddComponent<MeshComponent>();
            mc->SetMesh(sphereM);

            float hue = rnd(0, 1);
            float val = rnd(0.5f, 1.0f);
            mc->m_Color = Vec4(
                val * (0.5f + 0.5f * std::cos(hue * 6.2832f)),
                val * (0.5f + 0.5f * std::cos((hue + 0.333f) * 6.2832f)),
                val * (0.5f + 0.5f * std::cos((hue + 0.667f) * 6.2832f)),
                1.0f);

            scene.AddObject(obj);
            sceneObjects.push_back(obj.get());
        }

        // ── 中心视觉焦点物体 ──
        // 红色立方体
        auto cubeMesh = std::make_shared<Mesh>(Mesh::CreateCube(2.0f));
        auto centerCube = std::make_shared<GameObject>("CenterCube");
        centerCube->GetTransform().SetPosition(Vec3(0, 1.5f, 0));
        auto* cubeMc = centerCube->AddComponent<MeshComponent>();
        cubeMc->SetMesh(cubeMesh);
        cubeMc->m_Color = Vec4(1.0f, 0.3f, 0.2f, 1.0f);
        scene.AddObject(centerCube);
        sceneObjects.push_back(centerCube.get());

        // 金色圆环 — 用一圈小球体组成
        auto smallSphere = std::make_shared<Mesh>(Mesh::CreateSphere(0.15f, 6));
        for (int i = 0; i < 24; ++i) {
            float a = (float)i / 24.0f * 6.2832f;
            float rx = 3.0f, rz = 3.0f;
            auto ring = std::make_shared<GameObject>("Ring" + std::to_string(i));
            ring->GetTransform().SetPosition(Vec3(std::cos(a) * rx, 1.5f, std::sin(a) * rz));
            auto* rmc = ring->AddComponent<MeshComponent>();
            rmc->SetMesh(smallSphere);
            rmc->m_Color = Vec4(1.0f, 0.85f, 0.3f, 1.0f);
            scene.AddObject(ring);
            sceneObjects.push_back(ring.get());
        }

        ecsCreateTime_ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - startT).count();
        entityCount = N + 1 + 1 + 24; // ground + cube + ring
    }

    void HandleInput(float dt) {
        auto* gw = static_cast<GLFWwindow*>(window->GetNativeHandle());

        // ── F1 循环切换渲染后端 ──
        static bool wasF1 = false;
        bool isF1 = glfwGetKey(gw, GLFW_KEY_F1) == GLFW_PRESS;
        if (isF1 && !wasF1) {
            int next = ((int)currentBackend + 1) % 3;
            currentBackend = (RHI::RHIBackend)next;
            log.Info("Switching backend to: {}", BackendToName(currentBackend));
            backendDirty = true;
        }
        wasF1 = isF1;

        float sp = 15 * dt;
        Vec3 pos = camera.GetPosition(), fwd = camera.GetForward(), rgt = camera.GetRight();
        if (glfwGetKey(gw, GLFW_KEY_W) == GLFW_PRESS) { pos.x += fwd.x*sp; pos.y += fwd.y*sp; pos.z += fwd.z*sp; }
        if (glfwGetKey(gw, GLFW_KEY_S) == GLFW_PRESS) { pos.x -= fwd.x*sp; pos.y -= fwd.y*sp; pos.z -= fwd.z*sp; }
        if (glfwGetKey(gw, GLFW_KEY_A) == GLFW_PRESS) { pos.x -= rgt.x*sp; pos.y -= rgt.y*sp; pos.z -= rgt.z*sp; }
        if (glfwGetKey(gw, GLFW_KEY_D) == GLFW_PRESS) { pos.x += rgt.x*sp; pos.y += rgt.y*sp; pos.z += rgt.z*sp; }
        if (glfwGetKey(gw, GLFW_KEY_Q) == GLFW_PRESS) pos.y -= sp;
        if (glfwGetKey(gw, GLFW_KEY_E) == GLFW_PRESS) pos.y += sp;
        camera.SetPosition(pos);

        static bool drag = false;
        static double ddx=0, ddy=0;
        if (glfwGetMouseButton(gw, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            double mx, my;
            glfwGetCursorPos(gw, &mx, &my);
            if (!drag) { drag=true; ddx=mx; ddy=my; }
            float dx = (float)(mx - ddx) * 0.2f;
            float dy = (float)(my - ddy) * 0.2f;
            float pitch = max(-89.0f, min(89.0f, camera.GetPitch() - dy));
            camera.SetRotation(pitch, camera.GetYaw() + dx);
            ddx=mx; ddy=my;
        } else { drag = false; }

        if (glfwGetKey(gw, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            window->SetShouldClose(true);
    }

    void Run() {
        if (!window) return;
        auto* gw = static_cast<GLFWwindow*>(window->GetNativeHandle());
        auto* ctx = window->GetContext();

        auto last = std::chrono::high_resolution_clock::now();
        log.Info("Render loop started. F1=Switch Backend  ESC=Exit.");

        while (!window->ShouldClose()) {
            auto now = std::chrono::high_resolution_clock::now();
            float dt = (float)std::chrono::duration<double>(now - last).count();
            last = now;
            if (dt > 0.05f) dt = 0.05f;

            window->PollEvents();
            HandleInput(dt);

            // ── 热切换后端（占位：Vulkan/D3D12 工厂就绪后实现真正的切换） ──
            if (backendDirty) {
                backendDirty = false;
                log.Warn("[Backend] Hot-swap to {} requested. Implement factory teardown/init here.",
                         BackendToName(currentBackend));
            }

            // ── Viewport ──
            glfwGetFramebufferSize(gw, &fbW, &fbH);
            if (gl && fbW > 0 && fbH > 0) {
                gl->Viewport(0, 0, fbW, fbH);
                camera.SetAspectRatio((float)fbW / (float)fbH);
            }

            // ── 光源动画 ──
            lightAngle += dt * 20;
            float rad = lightAngle * 3.14159f / 180.0f;
            meshRenderer->SetLightPosition(Vec3(std::cos(rad)*25, 20, std::sin(rad)*25));

            // ═══════════════════════════════════════════════
            // GPU Pass 1: 主场景渲染（不透明）
            // ═══════════════════════════════════════════════
            if (gl) {
                gl->Enable(GL_DEPTH_TEST);
                gl->DepthFunc(GL_LESS);
                gl->DepthMask(GL_TRUE);
                gl->Disable(GL_BLEND);       // 不透明渲染，避免 Alpha 混合造成的遮罩感
                gl->Disable(GL_CULL_FACE);
                gl->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }

            passQueryIndices[0] = ctx->BeginGPUPass(passNames[0]);
            // 明亮的天空背景色
            ctx->ClearColor(0.08f, 0.12f, 0.22f, 1.0f);

            if (shader3D) shader3D->Bind();
            meshRenderer->Render(sceneObjects);
            if (passQueryIndices[0] >= 0) ctx->EndGPUPass(passQueryIndices[0]);

            // ═══════════════════════════════════════════════
            // GPU Pass 2: 网格 / 坐标轴辅助线（半透明）
            // ═══════════════════════════════════════════════
            passQueryIndices[1] = ctx->BeginGPUPass(passNames[1]);

            // 重新绑定着色器（Render() 末尾会 Unbind）
            if (shader3D) {
                shader3D->Bind();
                const Mat4& proj = camera.GetProjectionMatrix();
                const Mat4& view = camera.GetViewMatrix();
                shader3D->SetMat4("u_View", view.Data());
                shader3D->SetMat4("u_Projection", proj.Data());
                shader3D->SetMat4("u_Model", Mat4().Data());
                shader3D->SetInt("u_LightCount", 0);
                Vec3 white(1, 1, 1);
                shader3D->SetVec3("u_AmbientColor", &white.x);
            }

            if (gl) gl->Enable(GL_BLEND);  // 网格线启用透明度
            auto* batch = meshRenderer->GetBatch();
            if (batch) {
                float half = 25.0f;
                Vec4 gray(0.4f, 0.4f, 0.4f, 0.5f);
                Vec4 red(1.0f, 0.2f, 0.2f, 0.8f);
                Vec4 green(0.2f, 1.0f, 0.2f, 0.8f);
                Vec4 blue(0.2f, 0.2f, 1.0f, 0.8f);
                batch->Begin(PrimitiveType::Lines);
                for (int i = 0; i <= 50; ++i) {
                    float t = -half + (float)i / 50.0f * 2 * half;
                    bool isCenter = (i == 25);
                    batch->Line(Vec3(-half, -2.99f, t), Vec3(half, -2.99f, t),
                                isCenter ? red : gray);
                    batch->Line(Vec3(t, -2.99f, -half), Vec3(t, -2.99f, half),
                                isCenter ? blue : gray);
                }
                batch->Line(Vec3(0, -2.99f, 0), Vec3(3, -2.99f, 0), red);
                batch->Line(Vec3(0, -2.99f, 0), Vec3(0, 2.01f, 0), green);
                batch->Line(Vec3(0, -2.99f, 0), Vec3(0, -2.99f, 3), blue);
                batch->End();
            }
            if (passQueryIndices[1] >= 0) ctx->EndGPUPass(passQueryIndices[1]);

            // ═══════════════════════════════════════════════
            // GPU Pass 3: 重置渲染状态准备交换
            // ═══════════════════════════════════════════════
            passQueryIndices[2] = ctx->BeginGPUPass(passNames[2]);
            ctx->ResetPipelineState();
            if (passQueryIndices[2] >= 0) ctx->EndGPUPass(passQueryIndices[2]);

            // ═══════════════════════════════════════════════
            // GPU Pass 4: SwapBuffers + 统计
            // ═══════════════════════════════════════════════
            passQueryIndices[3] = ctx->BeginGPUPass(passNames[3]);

            ++frameCount;
            float fps = 1.0f / (dt > 0.001f ? dt : 0.001f);

            char buf[256];
            std::snprintf(buf, sizeof(buf), "R3DTest - %s | %zu objs | %.1f FPS | F1=Switch",
                         BackendToName(currentBackend), sceneObjects.size(), fps);
            glfwSetWindowTitle(gw, buf);

            if (frameCount % 60 == 0) {
                GPUProfileFrame gf;
                if (ctx->GetGPUProfileFrame(gf)) {
                    for (uint32 pi = 0; pi < gf.passCount; ++pi) {
                        std::printf("  [GPU Pass] %-12s: %.3f ms\n",
                                    gf.passes[pi].passName.c_str(),
                                    gf.passes[pi].elapsedMs);
                    }
                }
                std::printf("[Visual] Frame %llu | %s | %zu objs | %.1f FPS\n",
                           (unsigned long long)frameCount, BackendToName(currentBackend),
                           sceneObjects.size(), fps);
            }

            if (passQueryIndices[3] >= 0) ctx->EndGPUPass(passQueryIndices[3]);

            ctx->SwapBuffers();
        }
    }
};

void RunVisualRenderDemo() {
    std::cout << "\n=== Rendering3DTest: Visual 3D Demo ===" << std::endl;
    std::cout << " Creating window with 10,000 spheres + hot-reload backend..." << std::endl;
    VisualRenderDemo demo;
    if (!demo.Initialize()) {
        std::cerr << "[Visual] Init failed.\n";
        return;
    }
    std::printf("[ECS] %u objects in %.2f ms\n", demo.entityCount, demo.ecsCreateTime_ms);
    std::cout << " Controls: WASD=Move Q/E=Up/Dn RClick+Drag=Look F1=Switch Backend ESC=Exit\n";
    demo.Run();
    std::cout << "\n[Visual] " << demo.frameCount << " frames rendered.\n";
}

} // namespace Engine