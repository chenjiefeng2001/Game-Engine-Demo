#pragma once

/**
 * @file AssetBrowserPanel.h
 * @brief 资源文件浏览器面板 — 浏览项目文件系统中的资源文件
 *
 * 功能：
 *   - 树状目录浏览（从项目 assets/ 目录开始）
 *   - 文件图标 + 名称展示
 *   - 双击加载/打开
 *   - 拖拽导入（预留）
 */

#include "Engine/Types.h"
#include <string>
#include <functional>
#include <vector>

namespace Engine {

    class AssetBrowserPanel {
    public:
        AssetBrowserPanel() = default;
        ~AssetBrowserPanel() = default;

        AssetBrowserPanel(const AssetBrowserPanel&) = delete;
        AssetBrowserPanel& operator=(const AssetBrowserPanel&) = delete;

        // ── 配置 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        /** 设置资源根目录（如 "assets/"） */
        void SetRootPath(const std::string& path) { m_RootPath = path; }

        // ── 渲染 ──
        /** 在 OnImGui() 中调用 */
        void OnImGui();

        // ── 事件 ──
        using FileOpenCallback = std::function<void(const std::string& path)>;
        void SetFileOpenCallback(FileOpenCallback cb) { m_FileOpenCallback = std::move(cb); }

    private:
        void DrawDirectoryTree(const std::string& dirPath, const std::string& displayName);
        void DrawFileGrid(const std::string& dirPath);

        bool m_Visible = false;
        std::string m_RootPath = "assets/";
        std::string m_CurrentDir = "assets/";
        std::string m_SelectedFile;
        FileOpenCallback m_FileOpenCallback;
    };

} // namespace Engine
