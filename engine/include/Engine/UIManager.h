#pragma once

/**
 * @file UIManager.h
 * @brief UI 管理器单例 — 负责 Dear ImGui 的上下文生命周期
 *
 * 提供 Begin()/End() 接口封装每帧流程，管理 UI 显示/隐藏状态。
 *
 * 使用方式：
 *   UIManager::Init(window, glContext);          // 初始化一次
 *   while (running) {
 *       UIManager::Get()->Begin();               // 开始帧
 *       if (UIManager::Get()->IsVisible()) {     // 构建 UI
 *           ImGui::ShowDemoWindow();
 *       }
 *       UIManager::Get()->End();                 // 结束帧 + 渲染
 *   }
 *   UIManager::Shutdown();                       // 清理
 */

#include "Engine/Types.h"
#include <memory>

struct GLFWwindow;
struct GladGLContext;

namespace Engine {

    class UIManager {
    public:
        // ── 单例生命周期 ──

        /** 初始化 ImGui（OpenGL 上下文就绪后调用） */
        static void Init(GLFWwindow* window, GladGLContext& glContext);

        /** 关闭 ImGui，释放资源 */
        static void Shutdown();

        /** 获取单例实例 */
        static UIManager* Get() { return s_Instance; }

        // ── 帧生命周期 ──

        /**
         * @brief 开始 UI 帧
         *
         * 内部调用 ImGui::NewFrame()，并根据可见状态设置输入抢占。
         * 调用后即可构建 ImGui UI。
         */
        void Begin();

        /**
         * @brief 结束 UI 帧并渲染
         *
         * 内部调用 ImGui::Render() + ImGui_ImplOpenGL3_RenderDrawData()。
         * 如果 UI 处于隐藏状态，跳过绘制但保留内部状态更新。
         */
        void End();

        // ── 显示控制 ──

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const        { return m_Visible; }
        void ToggleVisibility()       { m_Visible = !m_Visible; }

        // ── 状态查询 ──

        bool IsInitialized() const { return m_Initialized; }

        /** ImGui 是否要求捕获鼠标（可见时有效） */
        bool WantCaptureMouse() const;

        /** ImGui 是否要求捕获键盘（可见时有效） */
        bool WantCaptureKeyboard() const;

        UIManager(const UIManager&) = delete;
        UIManager& operator=(const UIManager&) = delete;

        ~UIManager();

    private:
        UIManager() = default;

        static UIManager* s_Instance;
        static std::unique_ptr<UIManager> s_InstanceOwner;

        bool m_Initialized = false;
        bool m_Visible = true;
    };

} // namespace Engine
