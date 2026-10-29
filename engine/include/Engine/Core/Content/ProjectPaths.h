#pragma once

/**
 * @file ProjectPaths.h
 * @brief Content v2 M1-A：显式 ProjectRoot 与工程相对路径规范化
 *
 * 契约来源：docs/Roadmap-Decision-Record-2026-10-28.md §3.5（K4）。
 *
 * 本模块是路径解析的唯一入口。它**不读取进程工作目录**：
 * ProjectRoot 只能由调用方显式提供；legacy 兼容入口在边界处把 CWD
 * 一次性转换为显式根，之后所有解析都以该根为准。
 *
 * 策略（K4.8 要求显式，故在此写明）：
 *   - Manifest 相对路径：UTF-8、统一 `/`、无盘符/UNC/前导 `/`、无 `.`/`..`、
 *     无尾分隔；比较保留作者大小写，比较键按卷能力决定大小写敏感性。
 *   - `\\?\` 与 `\\.\` 前缀：**一律拒绝**（不静默截断、不静默失败）。
 *   - UNC 只允许作为显式 ProjectRoot；相对路径中出现 UNC 即拒绝。
 *   - 符号链接 / junction：默认拒绝经过链接的路径；显式允许时对**每个链接
 *     组件**解析 real path 并重新做包含检查。越界 → LinkEscape；无法解析
 *     （环、悬空链、不可读 reparse）→ LinkCycle。两者都带 real-path 诊断。
 *   - Windows long path（`\\?\`）在 M1-A 明确**不支持**：ProjectRoot 与工程
 *     相对路径都按 DevicePath 拒绝，而不是截断前缀后放行。
 */

#include <cstdint>
#include <string>
#include <string_view>

namespace Engine::Content {

enum class PathError : uint8_t {
    None = 0,
    Empty,
    InvalidCharacter,
    AbsolutePath,
    UncPath,
    DevicePath,
    NotAbsolute,
    DotSegment,
    DotDotSegment,
    RootNotAbsolute,
    RootMissing,
    RootNotDirectory,
    EscapesRoot,
    LinkNotAllowed,
    LinkEscape,
    /// 链接链不可解析：环、悬空目标或不可读的 reparse point。
    LinkCycle,
};

const char* ToString(PathError error);

/// 卷能力（显式判定，不按操作系统猜测）。
enum class CaseSensitivity : uint8_t { CaseSensitive = 0, CaseInsensitive };

const char* ToString(CaseSensitivity mode);

struct PathResolution {
    bool ok = false;
    PathError error = PathError::None;
    /// 成功时：Normalize 的规范工程相对路径，或 Resolve 的绝对路径。
    std::string path;
    /// 失败时的补充说明（K4.7：链接的 real path 越界证据）。
    /// 仅用于日志/错误通道；判定仍以 error 为准。
    std::string diagnostic;
};

/// 把输入规范化为 Manifest 规范形式（工程相对、`/` 分隔）。
/// 接受分隔符变体（`\`、重复 `/`、尾 `/`）并归一；语义违规（绝对/UNC/
/// `.`/`..`/空）返回结构化错误，不静默放行。
PathResolution NormalizeProjectRelative(std::string_view input);

/// 统一 PathKey（K4.5）：分隔符归一 + 按卷能力折叠大小写。
/// 仅用于相等性/冲突检测，不代替解析或规范形式校验。
std::string PathKey(std::string_view path, CaseSensitivity mode);

struct ProjectRootOptions {
    /// 默认 false：任何经过链接/junction 的路径都拒绝（K4.7）。
    bool allowLinks = false;
};

class ProjectRoot {
public:
    /// 显式创建工程根：必须是已存在的绝对目录。调用方负责提供该值；
    /// 本函数绝不回退到 CWD 或 Manifest 父目录。
    static bool Create(std::string_view explicitRoot, ProjectRoot* out,
                       PathError* error, std::string* diagnostic,
                       ProjectRootOptions options = {});

    bool IsValid() const { return !m_Root.empty(); }
    const std::string& Path() const { return m_Root; }
    CaseSensitivity CaseMode() const { return m_Case; }
    const ProjectRootOptions& Options() const { return m_Options; }

    /// 严格工程相对路径 → 绝对路径（含越界与链接检查）。
    PathResolution Resolve(std::string_view projectRelative) const;

    /// 相对输入按工程相对解析；绝对输入必须在根内（legacy 绝对路径兼容读）。
    PathResolution ResolveAny(std::string_view path) const;

    /// 根内绝对路径 → 规范工程相对路径；根外/相对输入 → 结构化错误。
    PathResolution ToProjectRelative(std::string_view absolute) const;

    /// 输入可为相对或根内绝对；统一得到规范工程相对路径（导入/清单归一用）。
    PathResolution ToCanonicalProjectPath(std::string_view input) const;

    /// 比较键：规范分隔符 + 按卷能力折叠大小写。仅用于相等性/冲突检测。
    std::string ComparisonKey(std::string_view path) const;
    bool KeysEqual(std::string_view a, std::string_view b) const;

private:
    PathResolution ResolveAbsolute(std::string_view normalizedAbsolute) const;
    bool CheckLinks(std::string_view absolute, PathError* error,
                    std::string* diagnostic) const;

    std::string m_Root;
    CaseSensitivity m_Case = CaseSensitivity::CaseSensitive;
    ProjectRootOptions m_Options;
};

} // namespace Engine::Content
