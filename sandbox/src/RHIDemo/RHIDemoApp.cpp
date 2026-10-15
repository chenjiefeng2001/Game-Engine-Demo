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
#if defined(_WIN32)
// 只有 Windows 分支直接调用原生访问器（X11/Wayland 分支走 dlsym，无需 GLFW 原生头，
// 也就无需 X11 / wayland-client 开发包）。
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <chrono>
#include <cstring>
#include <cstdio>
#include <thread>
#include <algorithm>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

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

#if defined(ENGINE_HAS_VULKAN)
static bool BindNativeWindow(RHI::VulkanDevice* dev, GLFWwindow* window);
#endif

bool RHIDemoApp::InitVulkan() {
    m_BackendName = "Vulkan";
#if defined(ENGINE_HAS_VULKAN)
    if (!RHI::HasVulkanSupport()) return false;
    m_RHIWindow = std::make_unique<RHI::RHIWindow>(1280, 720, "RHIDemo - Vulkan", RHI::RHIBackend::Vulkan);
    if (!m_RHIWindow->GetGLFWHandle()) return false;
    m_RHIWindow->Show();

m_Device = std::make_unique<RHI::VulkanDevice>();
      // 必须**先**声明原生句柄再 Initialize：非 Windows 上创建 surface 需要 display
      // 句柄，而 RHIWindow 的 GetHWND() 在这些平台恒为 nullptr。
      auto* vkDev = dynamic_cast<RHI::VulkanDevice*>(m_Device.get());
      if (!BindNativeWindow(vkDev, m_RHIWindow->GetGLFWHandle())) {
          std::printf("  [RHIDemo] Vulkan surface 句柄不可用，跳过该后端\n");
          return false;
      }
      // Initialize 的非 Windows 分支用传入的 windowHandle 覆盖 native.window，
      // 因此这里必须传真正的窗口 XID / wl_surface，而不是 GetHWND()。
      if (!m_Device->Initialize(vkDev->GetNativeWindow().window,
                                m_RHIWindow->GetWidth(), m_RHIWindow->GetHeight()))
          return false;
      m_GPUName = m_Device->GetDeviceName();
      std::printf("  [RHIDemo] Vulkan 1.3 设备已初始化 | %s\n", m_GPUName.c_str());
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

    m_Device = std::make_unique<RHI::D3D12Device>();
    if (!m_Device->Initialize(m_RHIWindow->GetHWND(), m_RHIWindow->GetWidth(), m_RHIWindow->GetHeight()))
        return false;
    m_GPUName = m_Device->GetDeviceName();
    std::printf("  [RHIDemo] D3D12 设备已初始化 | %s\n", m_GPUName.c_str());
    std::printf("  [RHIDemo] 注意：该路径尚未接线渲染 —— Run() 只轮询事件，不提交任何命令缓冲\n");
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

#if defined(ENGINE_HAS_VULKAN)
/**
 * 把 GLFW 窗口的原生句柄交给 VulkanDevice。
 *
 * RHIWindow 只在 _WIN32 下把 m_HWND 填成 HWND，其它平台显式置 nullptr。而
 * VulkanDevice::Initialize 的非 Windows 分支要求 display 句柄才能建 surface，
 * 于是 Linux 上永远拿不到 surface、CreateSwapChain 每帧返回空 —— 这条路径
 * 此前从未真正跑起来过。句柄必须由应用自己从 GLFW 取出并声明平台。
 *
 * GLFW 只导出**其构建时所用平台**的原生访问器，直接链接会在缺少该平台时链接失败，
 * 因此这里用 dlsym 在运行时解析。
 */
static bool BindNativeWindow(RHI::VulkanDevice* dev, GLFWwindow* window) {
    if (!dev || !window) return false;
#if defined(_WIN32)
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) { std::printf("  [RHIDemo] glfwGetWin32Window 返回空\n"); return false; }
    Engine::NativeWindow nw{};
    nw.window = hwnd;
    dev->SetNativeWindow(nw);
    return true;
#else
    // 优先 Wayland：合成器存在时 GLFW 会把窗口放在 Wayland 上。
    using GetWaylandDisplay = void* (*)(GLFWwindow*);
    using GetWaylandWindow  = void* (*)(GLFWwindow*);
    auto getWlDisplay = reinterpret_cast<GetWaylandDisplay>(dlsym(RTLD_DEFAULT, "glfwGetWaylandDisplay"));
    auto getWlSurface = reinterpret_cast<GetWaylandWindow>(dlsym(RTLD_DEFAULT, "glfwGetWaylandWindow"));
    if (getWlDisplay && getWlSurface) {
        void* disp = getWlDisplay(window);
        void* surf = getWlSurface(window);
        if (disp && surf) {
            Engine::NativeWindow nw{};
            nw.display = disp; nw.window = surf;
            nw.platform = Engine::NativeWindowPlatform::Wayland;
            dev->SetNativeWindow(nw);
            std::printf("  [RHIDemo] 原生句柄: Wayland\n");
            return true;
        }
    }
    using GetX11Display = void* (*)(GLFWwindow*);
    using GetX11Window  = unsigned long (*)(GLFWwindow*);
    auto getXDisplay = reinterpret_cast<GetX11Display>(dlsym(RTLD_DEFAULT, "glfwGetX11Display"));
    auto getXWindow  = reinterpret_cast<GetX11Window>(dlsym(RTLD_DEFAULT, "glfwGetX11Window"));
    if (getXDisplay && getXWindow) {
        void* disp = getXDisplay(window);
        unsigned long xid = getXWindow(window);
        if (disp && xid) {
            Engine::NativeWindow nw{};
            nw.display = disp;
            nw.window = reinterpret_cast<void*>(static_cast<uintptr_t>(xid));
            nw.platform = Engine::NativeWindowPlatform::Xlib;
            dev->SetNativeWindow(nw);
            std::printf("  [RHIDemo] 原生句柄: Xlib\n");
            return true;
        }
    }
    std::printf("  [RHIDemo] GLFW 未提供 X11/Wayland 原生句柄，无法创建 Vulkan surface\n");
    return false;
#endif
}
#endif // ENGINE_HAS_VULKAN

#if defined(ENGINE_HAS_VULKAN)
void RHIDemoApp::RenderVulkanFrame() {
    auto* vkDev = dynamic_cast<RHI::VulkanDevice*>(m_Device.get());
    if (!vkDev || !m_RHIWindow) return;

    if (!m_SwapChain) {
        RHI::SwapChainDesc desc;
        // GetWidth/GetHeight 返回 int，而 desc 用 uint32_t：显式转换避免符号转换告警。
        desc.width  = static_cast<uint32_t>(m_RHIWindow->GetWidth());
        desc.height = static_cast<uint32_t>(m_RHIWindow->GetHeight());
        desc.bufferCount = 2;
        desc.windowHandle = m_RHIWindow->GetHWND();
        m_SwapChain = vkDev->CreateSwapChain(desc);
        if (!m_SwapChain) {
            if (!m_SwapChainReported) {
                std::printf("  [RHIDemo] CreateSwapChain 失败，无法呈现\n");
                m_SwapChainReported = true;
            }
            return;
        }
    }

    auto* sc = dynamic_cast<RHI::VulkanSwapChain*>(m_SwapChain.get());
    if (!sc) return;

    // 顺序要点：必须先 acquire 才知道要渲染哪个 backbuffer，再录制，最后 present。
    const uint32_t imageIndex = sc->AcquireNextImage();
    if (imageIndex == UINT32_MAX) return;

    RHI::IRHITexture* backBuffer = sc->GetBackBuffer(imageIndex);
    if (!backBuffer) return;

    auto cmdList = m_Device->CreateCommandList(RHI::CommandListType::Direct);
    if (!cmdList) return;

    cmdList->Begin();
    RHI::RenderTargetDesc rt;
    rt.color[0] = backBuffer;
    rt.colorCount = 1;
    rt.clearColor[0] = 0.15f; rt.clearColor[1] = 0.20f;
    rt.clearColor[2] = 0.35f; rt.clearColor[3] = 1.0f;
    rt.clear = true;
    cmdList->BeginRendering(rt);
    // 此处尚未绘制几何：先只验证清除→呈现这条链路是否真的通。
    cmdList->EndRendering();
    cmdList->End();
    // 提交与呈现现在是两件事：Submit() 等 acquire、signal renderFinished 并提交，
    // Present() 只负责呈现。顺序不能反。
    cmdList->Submit();

    sc->Present();
}
#endif // ENGINE_HAS_VULKAN

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
            if (m_Backend == RHI::RHIBackend::Vulkan) {
#if defined(ENGINE_HAS_VULKAN)
                RenderVulkanFrame();
#endif
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
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