#pragma once

/**
 * @file MenuManager.h
 * @brief 游戏内置菜单管理器 — 管理运行时菜单的页面切换与状态
 *
 * 设计原则：
 *   - 全局单对象，所有菜单状态作为显式成员
 *   - 页式切换（MainMenu / Settings / Pause / GameUI）
 *   - 内置 ImGui 渲染（编辑器模式），预留运行时自定义渲染接口
 *   - 与 EngineEditor 分工：Editor = 编辑器UI，MenuManager = 运行时游戏菜单
 *   - 生命周期控制：仅在 Application::SetPlaying(true) 时激活，编辑器编辑状态下不渲染
 */

#include "Engine/Types.h"
#include "Engine/Core/Input.h"
#include <functional>
#include <string>
#include <vector>

namespace Engine {

    class MenuManager {
    public:
        // ============================================================
        // 菜单页枚举
        // ============================================================
        enum class Page : uint8 {
            None        = 0,     // 不在菜单中（全屏游戏）
            MainMenu    = 1,     // 主菜单（标题画面）
            Settings    = 2,     // 设置页
            Pause       = 3,     // 暂停菜单
            GameUI      = 4,     // 游戏内 HUD
            COUNT
        };

        // ============================================================
        // 菜单项定义（用于列表式菜单）
        // ============================================================
        struct MenuItem {
            std::string label;
            std::function<void()> action;
            bool enabled = true;
        };

        // ============================================================
        // 构造 / 析构
        // ============================================================
        MenuManager();
        ~MenuManager() = default;

        MenuManager(const MenuManager&) = delete;
        MenuManager& operator=(const MenuManager&) = delete;

        // ============================================================
        // 公共状态（可直接读写）
        // ============================================================

        /** 当前所在页面 — 默认 Page::None（不显示），由 Application::SetPlaying(true) 时设为 MainMenu */
        Page currentPage = Page::None;

        /** 菜单是否可见（全局开关，用于渐入/渐出过渡） */
        bool isVisible = true;

        /** 菜单整体透明度（0~1，用于过渡动画） */
        float alpha = 1.0f;

        // ============================================================
        // 生命周期 — 由 Application 调用
        // ============================================================

        /** 每帧更新动画/计时器/输入 */
        void OnUpdate(float32 dt);

        /**
         * @brief 绘制菜单（编辑器模式使用 ImGui，运行时使用自定义渲染）
         *
         * 当前实现使用 ImGui 绘制，便于快速调试。
         * 运行时可重写为自定义 OpenGL/Vulkan 渲染。
         */
        void OnImGui();

        /** 运行时自定义渲染（未来扩展） */
        void OnRender();

        // ============================================================
        // 页面导航
        // ============================================================

        /** 切换到指定页面 */
        void NavigateTo(Page page);

        /** 确认/选中当前项 */
        void Confirm();

        /** 返回上一页 */
        void Back();

        /** 获取当前页面的友好名称 */
        static const char* PageName(Page page);

        // ============================================================
        // 输入处理
        // ============================================================

        /** 处理键盘输入（由 Application 或 EngineEditor 转发） */
  void OnKeyPressed(KeyCode key);
        // ============================================================
        // 配置
        // ============================================================

        /** 注册退出游戏回调 */
        void SetQuitCallback(std::function<void()> cb) { m_QuitCallback = std::move(cb); }

        /** 注册开始游戏回调 */
        void SetStartGameCallback(std::function<void()> cb) { m_StartGameCallback = std::move(cb); }

    private:
        // ============================================================
        // 各页面绘制
        // ============================================================

        void DrawMainMenuPage();
        void DrawSettingsPage();
        void DrawPausePage();
        void DrawHUD();

        void DrawMenuItemList(const std::vector<MenuItem>& items, int32& selected);

        // ============================================================
        // 内部状态
        // ============================================================

        // 当前选中的菜单项索引
        int32 m_SelectedIndex = 0;

        // 最后一帧是否刚切换页面（用于重置选择索引）
        bool m_JustNavigated = false;

        // 上一页（用于 Back()）
        Page m_PreviousPage = Page::None;

        // 设置页状态
        int32   m_SelectedSetting = 0;
        float32 m_MasterVolume    = 1.0f;
        float32 m_SfxVolume       = 1.0f;
        bool    m_Fullscreen      = false;

        // 回调
        std::function<void()> m_QuitCallback;
        std::function<void()> m_StartGameCallback;
    };

} // namespace Engine