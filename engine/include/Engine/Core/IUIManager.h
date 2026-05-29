#pragma once

/**
 * @file IUIManager.h
 * @brief UI 管理器抽象接口 — RHI 无关的纯虚基类
 *
 * 所有 UI 系统（如 ImGui）通过此接口与引擎对接。
 * 具体实现由 IGraphicsFactory::CreateUIManager() 创建，
 * 核心引擎代码不再直接依赖 GLFW 和 OpenGL。
 *
 * 使用方式：
 *   auto ui = m_Factory.CreateUIManager();
 *   ui->Init(nativeWindowHandle, apiContext);
 *   while (running) {
 *       ui->Begin();
 *       // ... 绘制 UI ...
 *       ui->End();
 *   }
 *   ui->Shutdown();
 */

#include "Engine/Types.h"
#include <string>
#include <memory>

namespace Engine {

    class IUIManager {
    public:
        virtual ~IUIManager() = default;

        // ── 生命周期 ──

        /**
         * @brief 初始化 UI 系统
         * @param nativeWindow  原生窗口句柄（GLFWwindow*）
         * @param apiContext    图形 API 上下文指针（GladGLContext* 等）
         * @return 是否初始化成功
         */
        virtual bool Init(void* nativeWindow, void* apiContext) = 0;

        /** 关闭 UI 系统，释放所有资源 */
        virtual void Shutdown() = 0;

        /** 是否已初始化 */
        virtual bool IsInitialized() const = 0;

        // ── 帧生命周期 ──

        /** 开始新帧（内含 NewFrame + 输入抢占） */
        virtual void Begin() = 0;

        /** 结束当前帧并渲染 */
        virtual void End() = 0;

        // ── 显示控制 ──

        virtual void SetVisible(bool visible) = 0;
        virtual bool IsVisible() const = 0;
        virtual void ToggleVisibility() = 0;

        // ── 输入查询 ──

        virtual bool WantCaptureMouse() const = 0;
        virtual bool WantCaptureKeyboard() const = 0;

        // ── 缩放 ──

        virtual void SetScale(float scale) = 0;
        virtual float GetScale() const = 0;

        // ── 字体 ──

        virtual void LoadFont(const char* path, float size) = 0;
        virtual const std::string& GetFontPath() const = 0;
        virtual float GetFontSize() const = 0;
    };

} // namespace Engine
