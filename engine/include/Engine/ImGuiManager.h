#pragma once

/**
 * @file ImGuiManager.h
 * @brief Dear ImGui 集成管理器 — 负责 ImGui 的初始化、新帧、渲染与关闭
 *
 * 使用方式：
 *   1. 在 OpenGL 上下文创建后调用 Init()
 *   2. 每帧开始时调用 NewFrame()
 *   3. 在 NewFrame() 之后构建 ImGui UI
 *   4. 渲染结束时调用 Render()
 *   5. 程序退出时调用 Shutdown()
 *
 * 也可通过 Application 基类自动管理（调用 RegisterInitStep 注册 ImGui）。
 */

#include "Engine/Types.h"
#include <memory>

struct GLFWwindow;
struct GladGLContext;

namespace Engine {

    class ImGuiManager {
    public:
        ImGuiManager() = default;
        ~ImGuiManager();

        // 禁止拷贝
        ImGuiManager(const ImGuiManager&) = delete;
        ImGuiManager& operator=(const ImGuiManager&) = delete;

        // ── 生命周期 ──

        /**
         * @brief 初始化 Dear ImGui
         * @param window      GLFW 窗口句柄
         * @param glContext   GLAD OpenGL 上下文
         * @return true 初始化成功
         */
        bool Init(GLFWwindow* window, GladGLContext& glContext);

        /**
         * @brief 开始新帧（每帧开始时调用）
         * 调用后即可使用 ImGui::Begin() / ImGui::End() 构建 UI
         */
        void NewFrame();

        /**
         * @brief 渲染 ImGui 绘制数据（每帧渲染结束时调用）
         * 应在场景渲染完成后、SwapBuffers 之前调用
         */
        void Render();

        /**
         * @brief 关闭 ImGui，释放资源
         */
        void Shutdown();

        /**
         * @brief 查询是否已初始化
         */
        bool IsInitialized() const { return m_Initialized; }

    private:
        bool m_Initialized = false;
    };

} // namespace Engine
