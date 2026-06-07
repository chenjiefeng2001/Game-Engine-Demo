#pragma once

/**
 * @file ImGuiUIManager.h
 * @brief ImGui UI 管理器实现 — OpenGL 后端的 Dear ImGui 生命周期管理
 *
 * 实现 IUIManager 接口，封装 Dear ImGui 在 OpenGL 4.6 + GLFW3 上的初始化、
 * 帧管理、字体、缩放、引擎暗色主题等。
 *
 * 多窗口支持：通过 ImGuiConfigFlags_DockingEnable + ViewportsEnable
 * 启用官方多视口功能（ImGui docking 分支），面板可拖出主窗口成为独立 OS 窗口。
 */

#include "Engine/Core/IUIManager.h"
#include <string>
#include <memory>

namespace Engine {

    class ImGuiUIManager : public IUIManager {
    public:
        ImGuiUIManager() = default;
        ~ImGuiUIManager() override;

        ImGuiUIManager(const ImGuiUIManager&) = delete;
        ImGuiUIManager& operator=(const ImGuiUIManager&) = delete;

        // ── IUIManager 接口 ──

        bool Init(void* nativeWindow, void* apiContext) override;
        void Shutdown() override;
        bool IsInitialized() const override { return m_Initialized; }

        void Begin() override;
        void End() override;

        void SetVisible(bool visible) override { m_Visible = visible; }
        bool IsVisible() const override        { return m_Visible; }
        void ToggleVisibility() override       { m_Visible = !m_Visible; }

        bool WantCaptureMouse() const override;
        bool WantCaptureKeyboard() const override;

        void SetScale(float scale) override;
        float GetScale() const override { return m_Scale; }

        void LoadFont(const char* path, float size);
        const std::string& GetFontPath() const { return m_FontPath; }
        float GetFontSize() const { return m_FontSize; }

    private:
        /** 应用引擎暗色主题 */
        void ApplyEngineStyle(float scale);

        bool m_Initialized = false;
        bool m_Visible = true;
        float m_Scale = 1.0f;

        // 字体状态
        std::string m_FontPath;
        float m_FontSize = 16.0f;
        bool m_CjkFontAttempted = false;

        float m_PendingScale = -1.0f;
    };

} // namespace Engine
