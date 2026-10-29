#pragma once

/**
 * @file Utf8Path.h
 * @brief Content v2 M1-A：UTF-8 路径与 OS 原生路径的显式转换
 *
 * K4.2 规定清单路径是 UTF-8。MSVC 的 `fs::path(std::string)` 与
 * `std::ifstream(std::string)` 按活动代码页解释窄字符串（本机 ACP=936），
 * 非 ASCII 的 UTF-8 路径会在 OS 边界被错误编码。所有打开/探测/规范化的
 * 边界必须经过这里，而不是依赖进程代码页。
 *
 * 非法 UTF-8 输入返回空路径（调用方的 open/canonical 会结构化失败），
 * 不做"替换字符"或按 ACP 猜测——静默改写路径比明确失败更糟。
 */

#include <filesystem>
#include <string>
#include <string_view>

namespace Engine::Content {

std::filesystem::path PathFromUtf8(std::string_view utf8);

/// 反向转换始终使用 generic（`/` 分隔）形式：ProjectPaths 的内部不变量。
std::string PathToUtf8(const std::filesystem::path& path);

} // namespace Engine::Content
