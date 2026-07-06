#pragma once

/**
 * @file RHIWindow.h
 * @brief 多后端 RHI 窗口 — 统一管理 OpenGL/Vulkan/D3D12 窗口创建
 *
 * 设计原则：
 *   - 使用 GLFW 创建窗口，但不绑定特定图形 API
 *   - 根据后端类型选择性初始化 OpenGL 上下文或 Vulkan Surface
 *   - 提供 HWND 输出供 D3D12 使用
 */

#include "Engine/Types.h"
#include <memory>
#include <string>

// 前向声明 GLFWwindow（不包含 glfw3.h 避免与 GLAD 冲突）
struct GLFWwindow;

namespace Engine { namespace RHI {

enum class RHIBackend {
    OpenGL,
    Vulkan,
    D3D12,
    Count
};

class RHIWindow {
public:
    RHIWindow(int width, int height, const std::string& title, RHIBackend backend);
    ~RHIWindow();

    // 禁止拷贝
    RHIWindow(const RHIWindow&) = delete;
    RHIWindow& operator=(const RHIWindow&) = delete;

    // ── 通用接口 ──
    GLFWwindow* GetGLFWHandle() const noexcept { return m_Window; }
    int GetWidth() const noexcept { return m_Width; }
    int GetHeight() const noexcept { return m_Height; }
    RHIBackend GetBackend() const noexcept { return m_Backend; }

    void PollEvents();
    bool ShouldClose() const;
    void SetTitle(const std::string& title);
    void Show();
    void Hide();

    // ── OpenGL 路径 ──
    /** 初始化 OpenGL 上下文（仅当 Backend==OpenGL 时调用） */
    bool InitOpenGLContext();
    /** 交换 OpenGL 前后缓冲 */
    void SwapGLBuffers();

    // ── Vulkan 路径 ──
    /** 为 Vulkan 实例创建 Surface（仅当 Backend==Vulkan 时有效） */
    void* CreateVulkanSurface(void* vkInstance) const;

    // ── D3D12 路径 ──
    /** 获取 Win32 窗口句柄 */
    void* GetHWND() const noexcept { return m_HWND; }

private:
    GLFWwindow* m_Window = nullptr;
    int m_Width;
    int m_Height;
    std::string m_Title;
    RHIBackend m_Backend;

    // Win32 句柄（由 GLFW 创建窗口后获取）
    void* m_HWND = nullptr;
};

}} // namespace Engine::RHI