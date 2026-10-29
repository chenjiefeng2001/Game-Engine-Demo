/**
 * @file Utf8Path.cpp
 * @brief Content v2 M1-A：UTF-8 与原生路径转换的实现
 */

#include "Engine/Core/Content/Utf8Path.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Engine::Content {

std::filesystem::path PathFromUtf8(std::string_view utf8) {
#if defined(_WIN32)
    if (utf8.empty()) return std::filesystem::path();
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLength <= 0) return std::filesystem::path();
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                        static_cast<int>(utf8.size()), wide.data(),
                        wideLength);
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(std::string(utf8));
#endif
}

std::string PathToUtf8(const std::filesystem::path& path) {
#if defined(_WIN32)
    // 用 generic_wstring：ProjectPaths 的内部不变量是 `/` 分隔。
    const std::wstring wide = path.generic_wstring();
    if (wide.empty()) return {};
    const int utf8Length =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                            static_cast<int>(wide.size()), nullptr, 0,
                            nullptr, nullptr);
    if (utf8Length <= 0) return {};
    std::string out(static_cast<size_t>(utf8Length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        out.data(), utf8Length, nullptr, nullptr);
    return out;
#else
    return path.string();
#endif
}

} // namespace Engine::Content
