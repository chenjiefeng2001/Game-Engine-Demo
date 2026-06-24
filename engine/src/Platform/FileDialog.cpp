#include "Engine/Platform/FileDialog.h"

#include <cstdlib>
#include <cstring>
#include <array>
#include <cstdio>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

namespace Engine {

    // ═══════════════════════════════════════════════════════════════
    // 内部辅助
    // ═══════════════════════════════════════════════════════════════

    std::vector<std::string> FileDialog::ParseFilterExtensions(const std::string& filter) {
        std::vector<std::string> exts;
        // Windows 过滤器格式： "Description1\0*.ext1\0Description2\0*.ext2\0\0"
        // 解析由 \0 分隔的 token，提取包含 *. 的扩展名部分
        const char* p = filter.c_str();
        const char* end = p + filter.size();
        bool isDescription = true;
        while (p < end) {
            const char* tokenEnd = p;
            while (tokenEnd < end && *tokenEnd != '\0') ++tokenEnd;
            if (tokenEnd > p) {
                std::string token(p, tokenEnd);
                if (!isDescription) {
                    // 这是扩展名部分，如 "*.scene" 或 "*.*"
                    // 去除前导 "*.", 只保留扩展名
                    size_t starDot = token.find("*.");
                    if (starDot != std::string::npos) {
                        std::string ext = token.substr(starDot + 2);
                        // 处理多个扩展名如 "*.jpg;*.png"
                        size_t semi = ext.find(';');
                        if (semi != std::string::npos) {
                            // 分号分隔的多个扩展名
                            size_t start = 0;
                            while (true) {
                                size_t endSemi = ext.find(';', start);
                                std::string sub = (endSemi == std::string::npos)
                                    ? ext.substr(start)
                                    : ext.substr(start, endSemi - start);
                                if (!sub.empty()) {
                                    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
                                    exts.push_back(sub);
                                }
                                if (endSemi == std::string::npos) break;
                                start = endSemi + 1;
                            }
                        } else if (ext == "*") {
                            // "*.*" → 所有文件，用空字符串表示
                            exts.push_back("");
                        } else if (!ext.empty()) {
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                            exts.push_back(ext);
                        }
                    }
                }
                isDescription = !isDescription;
            }
            p = tokenEnd + 1;
        }
        return exts;
    }

    std::string FileDialog::GetExtension(const std::string& path) {
        size_t dot = path.rfind('.');
        if (dot == std::string::npos) return "";
        std::string ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }

    // ═══════════════════════════════════════════════════════════════
    // Linux 实现 — 使用 zenity / kdialog
    // ═══════════════════════════════════════════════════════════════

#if defined(__linux__) && !defined(_WIN32)

    static std::string BuildZenityFilter(const std::string& filter) {
        // zenity --file-filter 格式: "Name | *.ext1 *.ext2"
        std::string result;
        const char* p = filter.c_str();
        const char* end = p + filter.size();
        bool isDescription = true;
        while (p < end) {
            const char* tokenEnd = p;
            while (tokenEnd < end && *tokenEnd != '\0') ++tokenEnd;
            if (tokenEnd > p) {
                std::string token(p, tokenEnd);
                if (isDescription) {
                    if (!result.empty()) result += " | ";
                    result += token;
                } else {
                    // 将 "*.ext" 追加到描述后
                    if (!result.empty() && result.back() != ' ') result += " ";
                    result += token;
                }
                isDescription = !isDescription;
            }
            p = tokenEnd + 1;
        }
        return result;
    }

    static std::string ExecZenity(const std::string& cmd) {
        std::array<char, 4096> buffer{};
        std::string result;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            result += buffer.data();
        }
        int status = pclose(pipe);
        // zenity 返回 0 表示确认，1 表示取消
        if (status != 0) return "";
        // 去除末尾换行
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
        return result;
    }

    static std::string LinuxOpenFile(const std::string& filter,
                                     const std::string& defaultName) {
        std::string cmd = "zenity --file-selection --title=\"Open File\"";
        if (!defaultName.empty()) {
            cmd += " --filename=\"" + defaultName + "\"";
        }
        std::string zFilter = BuildZenityFilter(filter);
        if (!zFilter.empty()) {
            cmd += " --file-filter=\"" + zFilter + "\"";
        }
        return ExecZenity(cmd);
    }

    static std::string LinuxSaveFile(const std::string& filter,
                                     const std::string& defaultExt,
                                     const std::string& defaultName) {
        std::string cmd = "zenity --file-selection --save --title=\"Save File\"";
        if (!defaultName.empty()) {
            cmd += " --filename=\"" + defaultName + "\"";
        } else if (!defaultExt.empty()) {
            cmd += " --filename=\"untitled." + defaultExt + "\"";
        }
        std::string zFilter = BuildZenityFilter(filter);
        if (!zFilter.empty()) {
            cmd += " --file-filter=\"" + zFilter + "\"";
        }
        // zenity --save 不会自动附加扩展名，需要手动处理
        std::string path = ExecZenity(cmd);
        if (!path.empty() && !defaultExt.empty()) {
            std::string ext = FileDialog::GetExtension(path);
            if (ext.empty()) {
                path += "." + defaultExt;
            }
        }
        return path;
    }

#elif defined(__APPLE__)

    // ═══════════════════════════════════════════════════════════════
    // macOS 实现 — 使用 osascript
    // ═══════════════════════════════════════════════════════════════

    static std::string ExecOSAScript(const std::string& script) {
        std::array<char, 4096> buffer{};
        std::string result;
        std::string cmd = "osascript -e \"" + script + "\" 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            result += buffer.data();
        }
        int status = pclose(pipe);
        if (status != 0) return "";
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
        return result;
    }

    static std::string MacOpenFile(const std::string& filter,
                                   const std::string& defaultName) {
        auto exts = FileDialog::ParseFilterExtensions(filter);
        std::string typeList;
        for (size_t i = 0; i < exts.size(); ++i) {
            if (!exts[i].empty() && exts[i] != "*") {
                if (!typeList.empty()) typeList += ", ";
                typeList += "\"" + exts[i] + "\"";
            }
        }
        std::string script = R"(set theFile to choose file)";
        if (!typeList.empty()) {
            script = "set typeList to {" + typeList + "}\n"
                     "set theFile to choose file of type typeList\n";
        } else {
            script = "set theFile to choose file\n";
        }
        script += "set thePath to POSIX path of theFile\nreturn thePath";

        // osascript 直接返回路径
        std::string path = ExecOSAScript(script);
        return path;
    }

    static std::string MacSaveFile(const std::string& filter,
                                   const std::string& defaultExt,
                                   const std::string& defaultName) {
        std::string script = "set thePath to POSIX path of (choose file name";
        if (!defaultName.empty()) {
            script += " default name \"" + defaultName + "\"";
        }
        script += ")\nreturn thePath";

        std::string path = ExecOSAScript(script);
        if (!path.empty() && !defaultExt.empty()) {
            std::string ext = FileDialog::GetExtension(path);
            if (ext.empty()) {
                path += "." + defaultExt;
            }
        }
        return path;
    }

#else // _WIN32

    // ═══════════════════════════════════════════════════════════════
    // Windows 实现 — 使用 Win32 GetOpenFileName/GetSaveFileName
    // ═══════════════════════════════════════════════════════════════

    static std::string WinOpenFile(const std::string& filter,
                                   const std::string& defaultName) {
        char szFile[260] = {0};
        if (!defaultName.empty()) {
            std::strncpy(szFile, defaultName.c_str(), sizeof(szFile) - 1);
        }

        OPENFILENAMEA ofn = {0};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter.c_str();
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameA(&ofn)) {
            return std::string(szFile);
        }
        return "";
    }

    static std::string WinSaveFile(const std::string& filter,
                                   const std::string& defaultExt,
                                   const std::string& defaultName) {
        char szFile[260] = {0};
        if (!defaultName.empty()) {
            std::strncpy(szFile, defaultName.c_str(), sizeof(szFile) - 1);
        }

        OPENFILENAMEA ofn = {0};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter.c_str();
        ofn.nFilterIndex = 1;
        ofn.lpstrDefExt = defaultExt.empty() ? nullptr : defaultExt.c_str();
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

        if (GetSaveFileNameA(&ofn)) {
            return std::string(szFile);
        }
        return "";
    }

#endif

    // ═══════════════════════════════════════════════════════════════
    // 公开 API
    // ═══════════════════════════════════════════════════════════════

    std::string FileDialog::OpenFile(const std::string& filter,
                                     const std::string& defaultName) {
#if defined(__linux__) && !defined(_WIN32)
        return LinuxOpenFile(filter, defaultName);
#elif defined(__APPLE__)
        return MacOpenFile(filter, defaultName);
#else
        return WinOpenFile(filter, defaultName);
#endif
    }

    std::string FileDialog::SaveFile(const std::string& filter,
                                     const std::string& defaultExt,
                                     const std::string& defaultName) {
#if defined(__linux__) && !defined(_WIN32)
        return LinuxSaveFile(filter, defaultExt, defaultName);
#elif defined(__APPLE__)
        return MacSaveFile(filter, defaultExt, defaultName);
#else
        return WinSaveFile(filter, defaultExt, defaultName);
#endif
    }

} // namespace Engine