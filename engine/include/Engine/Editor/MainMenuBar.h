#pragma once

/**
 * @file MainMenuBar.h
 * @brief 主菜单栏 — 管理引擎的 File/Edit/View/Help 菜单
 *
 * 所有面板的可见性通过 PanelVisibility 结构体与 EngineEditor 联动。
 * 菜单快捷键：F5=运行, Ctrl+S=保存, Ctrl+Q=退出
 */

#include "Engine/Types.h"
#include <functional>
#include <string>

namespace Engine {

    /** 面板可见性状态（由 EngineEditor 填充，菜单栏用于切换） */
    struct PanelVisibility {
        bool sceneHierarchy  = true;
        bool inspector       = true;
        bool console         = false;
        bool performance     = true;
        bool contentBrowser  = false;
        bool assetBrowser    = false;
        bool viewport        = true;
    };

    class MainMenuBar {
    public:
        MainMenuBar() = default;
        ~MainMenuBar() = default;

        MainMenuBar(const MainMenuBar&) = delete;
        MainMenuBar& operator=(const MainMenuBar&) = delete;

        // ── 回调类型 ──
        using ActionCallback = std::function<void()>;

        // ── 配置 ──
        void SetPanelVisibility(PanelVisibility* vis) { m_Visibility = vis; }

        /** 注册"退出"回调 */
        void SetExitCallback(ActionCallback cb) { m_ExitCallback = std::move(cb); }

        /** 注册"运行"回调 */
        void SetPlayCallback(ActionCallback cb) { m_PlayCallback = std::move(cb); }

        // ── 渲染 ──
        /** 在 OnImGui() 中调用，绘制主菜单栏 */
        void OnImGui();

    private:
        void DrawFileMenu();
        void DrawEditMenu();
        void DrawViewMenu();
        void DrawHelpMenu();

        PanelVisibility* m_Visibility = nullptr;
        ActionCallback m_ExitCallback;
        ActionCallback m_PlayCallback;
    };

} // namespace Engine
