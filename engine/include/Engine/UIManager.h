#pragma once

/**
 * @file UIManager.h
 * @brief UI 管理器单例 — 负责 Dear ImGui 的上下文生命周期
 *
 * 提供 Begin()/End() 接口封装每帧流程，管理 UI 显示/隐藏状态。
 * 支持自定义字体热重载、全局缩放、引擎风格主题。
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
#include <string>

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

        // ── 全局缩放 ──

        /**
         * @brief 设置全局缩放比例（DPI 感知）
         * @param scale 缩放因子（1.0 = 100%）
         *
         * 调整字体大小、控件间距、窗口圆角等所有样式参数。
         * 在 Init() 之前调用可影响初始样式，运行时调用立即生效。
         */
        void SetScale(float scale);

        /** 获取当前缩放比例 */
        float GetScale() const { return m_Scale; }

        // ── 字体管理 ──

        /**
         * @brief 加载自定义字体（支持热重载）
         * @param path TTF/TTC 字体文件路径（为空则尝试系统默认中文字体）
         * @param size 字体大小（像素，0 表示使用当前缩放后的默认大小）
         * @return true 加载成功
         *
         * 可运行时多次调用实现热重载，字体纹理会自动重建。
         * 如果 path 为空，回退到系统中文字体搜索。
         */
        bool LoadFont(const char* path = nullptr, float size = 0.0f);

        /** 获取当前字体路径 */
        const std::string& GetFontPath() const { return m_FontPath; }

        /** 获取当前字体大小 */
        float GetFontSize() const { return m_FontSize; }

        UIManager(const UIManager&) = delete;
        UIManager& operator=(const UIManager&) = delete;

        ~UIManager();

    private:
        UIManager() = default;

        /** 应用引擎暗色主题 */
        void ApplyEngineStyle(float scale);

        static UIManager* s_Instance;
        static std::unique_ptr<UIManager> s_InstanceOwner;

        bool m_Initialized = false;
        bool m_Visible = true;
        float m_Scale = 1.0f;

        // 字体状态
        std::string m_FontPath;
        float m_FontSize = 16.0f;

        // 延迟字体重载
        float m_PendingScale = -1.0f;   ///< -1 表示无待处理的缩放
    };

} // namespace Engine
