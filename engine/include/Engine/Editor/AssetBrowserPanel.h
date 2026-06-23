#pragma once

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

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        void SetRootPath(const std::string& path) { m_RootPath = path; m_CurrentDir = path; }

        void OnImGui();

        using FileOpenCallback = std::function<void(const std::string& path)>;
        void SetFileOpenCallback(FileOpenCallback cb) { m_FileOpenCallback = std::move(cb); }

        using FileDragCallback = std::function<void(const std::string& path)>;
        void SetFileDragCallback(FileDragCallback cb) { m_FileDragCallback = std::move(cb); }

    private:
        void DrawDirectoryTree(const std::string& dirPath, const std::string& displayName);
        void DrawFileGrid(const std::string& dirPath);
        const char* GetFileIcon(const std::string& ext);

        bool m_Visible = false;
        std::string m_RootPath = "assets/";
        std::string m_CurrentDir = "assets/";
        std::string m_SelectedFile;
        FileOpenCallback m_FileOpenCallback;
        FileDragCallback m_FileDragCallback;
    };

} // namespace Engine