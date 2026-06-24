#pragma once

/**
 * @file FileDialog.h
 * @brief 跨平台文件对话框（打开/保存）抽象
 *
 * 用法：
 *   std::string path = FileDialog::OpenFile("Scene (*.scene)\0*.scene\0");
 *   if (!path.empty()) { ... }
 *
 * 对 Linux/macOS，内部使用 zenity / osascript 通过 popen 实现；
 * 对 Windows，使用原生 GetOpenFileName/GetSaveFileName。
 */

#include <string>
#include <vector>

namespace Engine {

    class FileDialog {
    public:
        /**
         * @brief 打开文件对话框
         * @param filter  Windows 格式的过滤器字符串，如 "Scene (*.scene)\0*.scene\0All\0*.*\0\0"
         *                对 Linux/macOS 会自动解析转换为 zenity 或 osascript 格式。
         * @param defaultName 默认文件名（可选）
         * @return 选中文件的路径，取消时返回空字符串
         */
        static std::string OpenFile(const std::string& filter,
                                    const std::string& defaultName = "");

        /**
         * @brief 保存文件对话框
         * @param filter    同上
         * @param defaultExt 默认扩展名（不含点号），如 "scene"
         * @param defaultName 默认文件名（可选）
         * @return 保存文件的路径，取消时返回空字符串
         */
        static std::string SaveFile(const std::string& filter,
                                    const std::string& defaultExt = "",
                                    const std::string& defaultName = "");

    private:
        // ── 内部辅助 ──

        /** 解析 Windows 风格过滤器，提取文件扩展名列表 */
        static std::vector<std::string> ParseFilterExtensions(const std::string& filter);

        /** 从完整路径中提取扩展名（不含点号，小写） */
        static std::string GetExtension(const std::string& path);
    };

} // namespace Engine