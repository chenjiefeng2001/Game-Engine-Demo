/**
 * @file RHIDemoApp.cpp
 * @brief 多后端 RHI 渲染演示 — 使用最简方式验证各后端的窗口初始化
 *
 * 因为 OpenGLGraphicsFactory 存在 windows.h 宏冲突 (CreateWindow)，
 * OpenGL 路径直接使用 GLFW + gladLoadGL 原生方式，绕过 factory。
 *
 * 验证目标：
 *   --opengl  (默认): GLFW + gladLoadGL → glClear → SwapBuffers
 *   --vulkan:         RHIWindow + VulkanDevice 初始化
 *   --d3d12:          RHIWindow + D3D12Device 初始化
 */

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define VK_NO_PROTOTYPES
#define GLFW_INCLUDE_NONE

#include "RHIDemoApp.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstring>
#include <cstdio>
#include <thread>
#include <algorithm>

#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Core/RHI/D3D12IRHIDevice.h"

namespace Engine {

static RHI::RHIBackend ParseBackend(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--vulkan") == 0) return RHI::RHIBackend::Vulkan;
        if (std::strcmp(argv[i], "--d3d12") == 0)  return RHI::RHIBackend::D3D12;
        if (std::strcmp(argv[i], "--opengl") == 0) return RHI::RHIBackend::OpenGL;
    }
    return RHI::RHIBackend::OpenGL;
}

RHIDemoApp::RHIDemoApp(RHI::RHIBackend backend) : m_Backend(backend) {}
RHIDemoApp::~RHIDemoApp() = default;

// ─── OpenGL 路径（使用原生 GLFW + glad）───

static struct {
    GLFWwindow* window = nullptr;
    GladGLContext gl;
} s_OpenGL;

bool RHIDemoApp::InitOpenGL() {
    m_BackendName = "OpenGL";

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(1280, 720, "RHIDemo - OpenGL", nullptr, nullptr);
    if (!win) return false;
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    if (!gladLoadGLContext(&s_OpenGL.gl, glfwGetProcAddress)) {
        glfwDestroyWindow(win);
        return false;
    }

    s_OpenGL.window = win;

    const char* ren = (const char*)s_OpenGL.gl.GetString(GL_RENDERER);
    const char* ver = (const char*)s_OpenGL.gl.GetString(GL_VERSION);
    m_GPUName = ren ? ren : "Unknown";
    std::printf("  [RHIDemo] ✅ OpenGL %s | %s\n", ver ? ver : "?", m_GPUName.c_str());
    glfwShowWindow(win);
    return true;
}

bool RHIDemoApp::InitVulkan() {
    m_BackendName = "Vulkan";
#if defined(ENGINE_HAS_VULKAN)
    if (!RHI::HasVulkanSupport()) return false;
    m_RHIWindow = std::make_unique<RHI::RHIWindow>(1280, 720, "RHIDemo - Vulkan", RHI::RHIBackend::Vulkan);
    if (!m_RHIWindow->GetGLFWHandle()) return false;
    m_RHIWindow->Show();

    auto dev = std::make_unique<RHI::VulkanDevice>();
    if (!dev->Initialize(m_RHIWindow->GetHWND(), m_RHIWindow->GetWidth(), m_RHIWindow->GetHeight()))
        return false;
    m_GPUName = dev->GetDeviceName();
    std::printf("  [RHIDemo] ✅ Vulkan 1.3 | %s\n", m_GPUName.c_str());
    return true;
#else
    return false;
#endif
}

bool RHIDemoApp::InitD3D12() {
    m_BackendName = "D3D12";
#if defined(ENGINE_HAS_D3D12)
    if (!RHI::HasD3D12Support()) return false;
    m_RHIWindow = std::make_unique<RHI::RHIWindow>(1280, 720, "RHIDemo - D3D12", RHI::RHIBackend::D3D12);
    if (!m_RHIWindow->GetGLFWHandle()) return false;
    m_RHIWindow->Show();

    auto dev = std::make_unique<RHI::D3D12Device>();
    if (!dev->Initialize(m_RHIWindow->GetHWND(), m_RHIWindow->GetWidth(), m_RHIWindow->GetHeight()))
        return false;
    m_GPUName = dev->GetDeviceName();
    std::printf("  [RHIDemo] ✅ D3D12 | %s\n", m_GPUName.c_str());
    return true;
#else
    return false;
#endif
}

bool RHIDemoApp::Initialize() {
    if (!glfwInit()) return false;

    switch (m_Backend) {
        case RHI::RHIBackend::OpenGL: return InitOpenGL();
        case RHI::RHIBackend::Vulkan: return InitVulkan();
        case RHI::RHIBackend::D3D12:  return InitD3D12();
        default: return false;
    }
}

void RHIDemoApp::Run() {
    auto lastTime = std::chrono::high_resolution_clock::now();
    int fpsCount = 0;
    float fpsAccum = 0.0f;

    while (true) {
        if (m_Backend == RHI::RHIBackend::OpenGL) {
            if (!s_OpenGL.window || glfwWindowShouldClose(s_OpenGL.window)) break;
            glfwPollEvents();
            if (glfwGetKey(s_OpenGL.window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(s_OpenGL.window, GLFW_TRUE);

            s_OpenGL.gl.ClearColor(0.15f, 0.2f, 0.35f, 1.0f);
            s_OpenGL.gl.Clear(GL_COLOR_BUFFER_BIT);
            glfwSwapBuffers(s_OpenGL.window);
        } else {
            if (!m_RHIWindow || m_RHIWindow->ShouldClose()) break;
            m_RHIWindow->PollEvents();
            if (glfwGetKey(m_RHIWindow->GetGLFWHandle(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(m_RHIWindow->GetGLFWHandle(), GLFW_TRUE);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        ++m_FrameCount;
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        fpsAccum += dt;
        fpsCount++;
        if (fpsAccum >= 1.0f) {
            if (m_Backend == RHI::RHIBackend::OpenGL)
                std::printf("[RHIDemo] FPS=%.1f | Frame=%llu\n", fpsCount / fpsAccum, (unsigned long long)m_FrameCount);
            fpsAccum = 0.0f;
            fpsCount = 0;
        }
    }
    std::printf("  [RHIDemo] Exited after %llu frames\n", (unsigned long long)m_FrameCount);
}

} // namespace Engine

int main(int argc, char* argv[]) {
    Engine::RHI::RHIBackend backend = Engine::ParseBackend(argc, argv);
    const char* names[] = {"OpenGL", "Vulkan", "D3D12"};
    std::printf("\n=== RHIDemo ===\nBackend: %s\n", names[(int)backend]);

    Engine::RHIDemoApp app(backend);
    if (!app.Initialize()) {
        std::fprintf(stderr, "RHIDemo init failed\n");
        return 1;
    }
    app.Run();
    return 0;
}