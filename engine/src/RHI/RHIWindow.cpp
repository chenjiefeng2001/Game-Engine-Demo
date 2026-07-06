/**
 * @file RHIWindow.cpp
 * @brief 多后端 RHI 窗口实现 — 基于 GLFW
 *
 * 三种后端的窗口创建策略：
 *   - OpenGL: GLFW 正常创建窗口 + OpenGL 上下文
 *   - Vulkan:  GLFW_NO_API 创建窗口，外部创建 Vulkan Surface
 *   - D3D12:  GLFW_NO_API 创建窗口，外部使用 HWND 创建 SwapChain
 *
 * 注意：GLFW_INCLUDE_VULKAN 必须在 #include <GLFW/glfw3.h> 之前定义
 */

#include "Engine/Core/RHI/RHIWindow.h"

// For CreateVulkanSurface we need the Vulkan types from GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdio>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace Engine { namespace RHI {

RHIWindow::RHIWindow(int width, int height, const std::string& title, RHIBackend backend)
    : m_Width(width)
    , m_Height(height)
    , m_Title(title)
    , m_Backend(backend)
{
    if (!glfwInit()) {
        std::fprintf(stderr, "[RHIWindow] Failed to initialize GLFW\n");
        return;
    }

    if (backend == RHIBackend::Vulkan || backend == RHIBackend::D3D12) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    } else {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_Window) {
        std::fprintf(stderr, "[RHIWindow] Failed to create GLFW window\n");
        glfwTerminate();
        return;
    }

    m_HWND = glfwGetWin32Window(m_Window);

    std::printf("  [RHIWindow] Created %dx%d window (backend=%d)\n",
                width, height, static_cast<int>(backend));
}

RHIWindow::~RHIWindow() {
    if (m_Window) {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }
}

void RHIWindow::PollEvents() { glfwPollEvents(); }
bool RHIWindow::ShouldClose() const { return m_Window ? glfwWindowShouldClose(m_Window) : true; }

void RHIWindow::SetTitle(const std::string& title) {
    m_Title = title;
    if (m_Window) glfwSetWindowTitle(m_Window, title.c_str());
}

void RHIWindow::Show() { if (m_Window) glfwShowWindow(m_Window); }
void RHIWindow::Hide() { if (m_Window) glfwHideWindow(m_Window); }

// ── OpenGL ──

bool RHIWindow::InitOpenGLContext() {
    if (!m_Window || m_Backend != RHIBackend::OpenGL) return false;
    glfwMakeContextCurrent(m_Window);
    glfwSwapInterval(1);
    return true;
}

void RHIWindow::SwapGLBuffers() {
    if (m_Window && m_Backend == RHIBackend::OpenGL) glfwSwapBuffers(m_Window);
}

// ── Vulkan ──

void* RHIWindow::CreateVulkanSurface(void* vkInstance) const {
#if defined(ENGINE_HAS_VULKAN)
    if (!m_Window || !vkInstance || m_Backend != RHIBackend::Vulkan) return nullptr;

    VkInstance instance = *(const VkInstance*)vkInstance;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = glfwCreateWindowSurface(instance, m_Window, nullptr, &surface);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[RHIWindow] glfwCreateWindowSurface failed: %d\n", result);
        return nullptr;
    }
    // VkSurfaceKHR is a non-dispatchable handle (usually uint64_t on Win64).
    // Store it as void* via uintptr_t.
    return (void*)(uintptr_t)surface;
#else
    (void)vkInstance;
    return nullptr;
#endif
}

}} // namespace Engine::RHI