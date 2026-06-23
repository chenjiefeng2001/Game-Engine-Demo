#pragma once

/**
 * @file DockspaceBuilder.h
 * @brief 全屏 Docking 工作台 — 实现 Unity/Unreal 风格的可拖拽面板系统
 *
 * 在 Application::OnImGui() 的最外层调用：
 * @code
 *   DockspaceBuilder dockspace;
 *   dockspace.Begin("MainDockspace");
 *
 *   // 在此之间渲染所有编辑器面板
 *   m_HierarchyPanel.OnImGui();
 *   m_InspectorPanel.OnImGui();
 *   m_ConsolePanel.OnImGui();
 *   ViewportPanel();
 *
 *   dockspace.End();
 * @endcode
 *
 * Begin() 内部会：
 *   1. 创建一个覆盖整个视口的透明窗口（无背景、无标题栏、无滚动条）
 *   2. 调用 ImGui::DockSpaceOverViewport() 挂载 Docking 系统
 *   3. 首次运行时调用 SetupDefaultLayout() 创建初始面板布局
 */

#include "Engine/Types.h"
#include <cstdint>

namespace Engine {

    class DockspaceBuilder {
    public:
        DockspaceBuilder();
        ~DockspaceBuilder();

        DockspaceBuilder(const DockspaceBuilder&) = delete;
        DockspaceBuilder& operator=(const DockspaceBuilder&) = delete;

        /**
         * @brief 开始 Dockspace 帧
         *
         * 创建全屏透明窗口，调用 DockSpaceOverViewport()。
         * 所有编辑器面板应在此调用之后、End() 之前渲染。
         *
         * @param dockspaceId  Dockspace 的唯一标识符（用于持久化布局）
         */
        void Begin(const char* dockspaceId = "MainDockspace");

        /** @brief 结束 Dockspace 帧 */
        void End();

        /**
         * @brief 设置初始布局（仅在首次运行时生效）
         *
         * 默认布局：
         *   ┌────────────┬──────────────┬────────────┐
         *   │            │              │            │
         *   │ Hierarchy  │   Viewport   │ Inspector  │
         *   │   (Left)   │   (Center)   │  (Right)   │
         *   │            │              │            │
         *   ├────────────┴──────────────┴────────────┤
         *   │              Console (Bottom)          │
         *   └────────────────────────────────────────┘
         */
        void SetupDefaultLayout();

        /** @brief 查询是否已完成初始布局 */
        bool IsLayoutInitialized() const { return m_LayoutInitialized; }

        /** @brief 标记布局为未初始化，下次 Begin() 时重新创建 */
        void ResetLayout() { m_LayoutInitialized = false; }

    private:
        /** Dockspace 的 ImGuiID */
        uint64 m_DockspaceId = 0;

        /** 是否已完成初始布局设置 */
        bool m_LayoutInitialized = false;
    };

} // namespace Engine