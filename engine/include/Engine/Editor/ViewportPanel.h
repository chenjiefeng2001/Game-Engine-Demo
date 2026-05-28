#pragma once

/**
 * @file ViewportPanel.h
 * @brief 视口面板 — 游戏主渲染视图的 ImGui 容器
 *
 * 功能：
 *   - 嵌入游戏渲染结果到 ImGui 窗口
 *   - 处理窗口大小变化 → 通知渲染上下文 Resize
 *   - 鼠标事件坐标转换（屏幕 → 世界）
 *   - 支持 Gizmo 操作（预留）
 */

#include "Engine/Types.h"
#include <functional>
#include <string>

namespace Engine {

    class IRenderContext;

    class ViewportPanel {
    public:
        ViewportPanel() = default;
        ~ViewportPanel() = default;

        ViewportPanel(const ViewportPanel&) = delete;
        ViewportPanel& operator=(const ViewportPanel&) = delete;

        // ── 配置 ──
        void SetRenderContext(IRenderContext* ctx) { m_RenderContext = ctx; }

        // ── 尺寸查询 ──
        float32 GetWidth() const  { return m_Width; }
        float32 GetHeight() const { return m_Height; }
        bool    IsHovered() const { return m_Hovered; }
        bool    IsFocused() const { return m_Focused; }

        /** 获取视口坐标 → 窗口坐标的偏移 */
        float32 GetViewX() const { return m_ViewX; }
        float32 GetViewY() const { return m_ViewY; }

        // ── 渲染 ──
        /** 在 OnImGui() 中调用，绘制视口窗口 */
        void OnImGui();

        // ── 事件 ──
        using ResizeCallback = std::function<void(int32 width, int32 height)>;
        void SetResizeCallback(ResizeCallback cb) { m_ResizeCallback = std::move(cb); }

    private:
        IRenderContext* m_RenderContext = nullptr;

        // 上次渲染时的视口尺寸
        float32 m_Width  = 0.0f;
        float32 m_Height = 0.0f;
        float32 m_ViewX  = 0.0f;
        float32 m_ViewY  = 0.0f;
        bool    m_Hovered = false;
        bool    m_Focused = false;

        ResizeCallback m_ResizeCallback;
    };

} // namespace Engine
