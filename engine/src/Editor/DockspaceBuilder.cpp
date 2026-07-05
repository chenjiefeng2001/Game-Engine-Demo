#include "Engine/DockspaceBuilder.h"
#include <imgui.h>

namespace Engine {

    DockspaceBuilder::DockspaceBuilder() = default;

    DockspaceBuilder::~DockspaceBuilder() = default;

    void DockspaceBuilder::Begin(const char* dockspaceId) {
        // ── 1. 挂载 DockSpace（全屏覆盖主视口） ──
        // DockSpaceOverViewport() 内部会创建一个覆盖整个视口的透明窗口，
        // 然后调用 DockSpace() 挂载 Docking 系统。
        // ImGuiDockNodeFlags_PassthruCentralNode 让中央节点的背景透明，
        // 使得场景渲染内容能透过面板间隙可见。
        ImGuiID id = ImGui::GetID(dockspaceId);
        m_DockspaceId = static_cast<uint64>(ImGui::DockSpaceOverViewport(
            id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode));

        // ── 2. 首次运行 → 创建默认布局 ──
        if (!m_LayoutInitialized) {
            SetupDefaultLayout();
            m_LayoutInitialized = true;
        }
    }

    void DockspaceBuilder::End() {
        // DockSpaceOverViewport 不需要手动 End()
        // 窗口在 DockSpaceOverViewport 内部自动管理
    }

    // ============================================================
    // 默认布局说明：
    //
    // 我们将 Dockspace 分割为以下区域：
    //
    //   ┌──────────────┬──────────────────────┬──────────────┐
    //   │              │                      │              │
    //   │  Left Side   │      Center          │  Right Side  │
    //   │  (Hierarchy) │    (Viewport)        │ (Inspector)  │
    //   │              │                      │              │
    //   ├──────────────┴──────────────────────┴──────────────┤
    //   │                   Bottom (Console)                 │
    //   └────────────────────────────────────────────────────┘
    //
    // 使用 ImGui::Splitter 模拟方式在 ImGui master 分支实现初始布局：
    //   1. 在主 Dockspace 上分割出底部区域 (25%)
    //   2. 在上部区域分割出左右侧面板
    // 由于 ImGui master 分支没有公开的 DockBuilder API，
    // 我们通过创建带窗口名称的面板来达到初始布局目的。
    //
    // 注意：ImGui master 分支的 Docking 系统会自动从 imgui.ini 恢复布局，
    //      如果用户已经调整过布局，这里的初始分割会被覆盖。
    // ============================================================

    void DockspaceBuilder::SetupDefaultLayout() {
        // ImGui master 分支没有公开的 DockBuilder API (仅在 docking 分支有)，
        // 但 Docking 系统本身已启用 (ImGuiConfigFlags_DockingEnable)。
        //
        // 布局恢复策略：
        //   1. 布局由 imgui.ini 自动持久化，用户调整后重启自动恢复
        //   2. 我们只需要确保第一个运行时有合理的初始布局即可
        //
        // 通过 DockSpaceOverViewport() 创建的 Dockspace 默认是一个单一中央节点。
        // 如果没有任何窗口被显式 Dock，所有窗口都会浮动在中央区域。
        //
        // 对于需要初始分割布局的场景，可以考虑以下替代方案：
        //   - 切换到 imgui docking 分支启用 DockBuilder API
        //   - 在单帧中手动创建带 Splitter 的窗口系统
        //   - 依赖用户手动拖拽面板形成布局（imgui.ini 会记住）
        //
        // 当前实现启用 Docking 能力，面板会自动吸附到彼此边缘形成分割。
        // 用户只需拖拽 "Scene Hierarchy" 到左边缘、"Console" 到底部，
        // 布局即会形成上述结构，并被 imgui.ini 持久化。
    }

} // namespace Engine