/**
 * @file ProjectPaths.cpp
 * @brief Content v2 M1-A：ProjectRoot 与工程相对路径规范化的唯一实现
 */

#include "Engine/Core/Content/ProjectPaths.h"

#include "Engine/Core/Content/Utf8Path.h"

#include <filesystem>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Engine::Content {

namespace {

std::string WithForwardSlashes(std::string_view input) {
    std::string out(input);
    for (char& c : out)
        if (c == '\\') c = '/';
    return out;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

bool IsAsciiAlpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool HasDrivePrefix(std::string_view value) {
    return value.size() >= 2 && IsAsciiAlpha(value[0]) && value[1] == ':';
}

bool IsAbsoluteForm(std::string_view value) {
    return (!value.empty() && value[0] == '/') || HasDrivePrefix(value);
}

bool HasDevicePrefix(std::string_view normalized) {
    return StartsWith(normalized, "//?/") || StartsWith(normalized, "//./");
}

PathResolution Fail(PathError error, std::string diagnostic = {}) {
    return PathResolution{false, error, {}, std::move(diagnostic)};
}

std::string StripTrailingSeparator(std::string path) {
    while (path.size() > 1 && path.back() == '/') {
        // "C:/" 是卷根；砍成 "C:" 会变成 drive-relative。
        if (path.size() == 3 && HasDrivePrefix(path)) break;
        path.pop_back();
    }
    return path;
}

std::string LowerAscii(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}

/// K4.5：大小写不敏感卷使用 ordinal-ignore-case。Windows 的 ordinal
/// 比较按 UTF-16 码元做简单大写映射；只折 ASCII 会把 `Ä`/`ä`、`Тест`/`тест`
/// 当成不同键，使重复检测与包含检查在非 ASCII 路径上留下洞。
/// 非法 UTF-8 输入退回确定性的 ASCII 折叠（不改变字节数）。
std::string FoldOrdinal(std::string value) {
#if defined(_WIN32)
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (wideLength <= 0) return LowerAscii(std::move(value));
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), wide.data(),
                        wideLength);
    for (wchar_t& ch : wide) {
        wchar_t mapped[4] = {};
        // 只接受 1:1 映射：ordinal 大小写是每码元的简单映射；
        // 展开映射（如连字）在 NTFS 上也不是大小写变体。
        if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, &ch, 1,
                          mapped, 4, nullptr, nullptr, 0) == 1) {
            ch = mapped[0];
        }
    }
    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                               wideLength, nullptr, 0,
                                               nullptr, nullptr);
    if (utf8Length <= 0) return LowerAscii(std::move(value));
    std::string out(static_cast<size_t>(utf8Length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, out.data(),
                        utf8Length, nullptr, nullptr);
    return out;
#else
    (void)value;
    return value;   // POSIX 卷为大小写敏感，键保持原样
#endif
}

/// 比较键的分隔符规范化：`\` → `/`、折叠重复分隔、去掉尾分隔。
/// PathKey 必须让 `assets\\a`、`assets//a/`、`assets/a` 互为同一键，
/// 否则重复键检测会在分隔符变体上留下洞。
///
/// 根前缀必须保留：`//server/share`（UNC）与 `/server/share`（当前盘 rooted）
/// 是两个命名空间。把前导分隔符一并折叠掉，会让 UNC ProjectRoot（K4.8 允许）
/// 把当前盘的 `/server/share/...` 误判为根内路径。
std::string CollapseSeparators(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    size_t i = 0;
    const bool unc = input.size() >= 2 &&
                     (input[0] == '/' || input[0] == '\\') &&
                     (input[1] == '/' || input[1] == '\\');
    if (unc) {
        out = "//";
        i = 2;
    } else if (!input.empty() && (input[0] == '/' || input[0] == '\\')) {
        out = "/";
        i = 1;
    }
    bool lastSlash = !out.empty();
    for (; i < input.size(); ++i) {
        char c = input[i];
        if (c == '\\') c = '/';
        if (c == '/') {
            if (lastSlash) continue;
            out.push_back('/');
            lastSlash = true;
        } else {
            out.push_back(c);
            lastSlash = false;
        }
    }
    while (!out.empty() && out.back() == '/') {
        if (out.size() == 1) break;                    // "/" 是根本身
        if (out.size() == 2 && unc) break;             // "//" 是 UNC 前缀
        out.pop_back();
    }
    return out;
}

std::string ComparisonKeyFor(CaseSensitivity mode, std::string_view path) {
    std::string key = CollapseSeparators(path);
    return mode == CaseSensitivity::CaseInsensitive
               ? FoldOrdinal(std::move(key))
               : key;
}

bool PrefixEquals(CaseSensitivity mode, std::string_view prefix,
                  std::string_view value) {
    if (value.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char a = prefix[i];
        char b = value[i];
        if (mode == CaseSensitivity::CaseInsensitive) {
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) return false;
    }
    return true;
}

bool ContainsPath(CaseSensitivity mode, const std::string& root,
                  std::string_view candidate) {
    const std::string rootKey = ComparisonKeyFor(mode, root);
    const std::string candidateKey = ComparisonKeyFor(mode, candidate);
    if (candidateKey == rootKey) return true;
    if (rootKey.back() == '/') return StartsWith(candidateKey, rootKey);
    return StartsWith(candidateKey, rootKey + "/");
}

PathResolution RelativeOf(CaseSensitivity mode, const std::string& root,
                          std::string_view candidate) {
    if (!PrefixEquals(mode, root, candidate)) return Fail(PathError::EscapesRoot);
    size_t start = root.size();
    if (start == candidate.size()) return Fail(PathError::Empty);
    if (root.back() != '/') {
        if (candidate[start] != '/') return Fail(PathError::EscapesRoot);
        ++start;
    }
    std::string relative(candidate.substr(start));
    if (relative.empty()) return Fail(PathError::Empty);
    return PathResolution{true, PathError::None, std::move(relative)};
}

bool IsLinkAt(const std::string& path) {
#if defined(_WIN32)
    const std::wstring wide = PathFromUtf8(path).wstring();
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code ec;
    const fs::file_status status = fs::symlink_status(fs::path(path), ec);
    return !ec && fs::is_symlink(status);
#endif
}

/// 卷能力的显式探测：查询根目录的 per-directory case sensitivity 标志。
/// 查询不受支持时按该文件系统的 Windows 语义视为大小写不敏感，
/// 而不是按"操作系统是 Windows"推断。非 Windows 平台为大小写敏感。
CaseSensitivity ProbeCaseSensitivity(const std::string& utf8Root,
                                     std::string* diagnostic) {
#if defined(_WIN32)
    const std::wstring wide = PathFromUtf8(utf8Root).wstring();
    HANDLE handle = CreateFileW(
        wide.c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (diagnostic) *diagnostic = "case probe: root handle unavailable -> insensitive";
        return CaseSensitivity::CaseInsensitive;
    }
    struct CaseSensitiveInfo {
        ULONG Flags;
    } info{};
    constexpr int kFileCaseSensitiveInfo = 23;   // FILE_INFO_BY_HANDLE_CLASS
    constexpr ULONG kCaseSensitiveDir = 0x1;     // FILE_CS_FLAG_CASE_SENSITIVE_DIR
    const BOOL ok = GetFileInformationByHandleEx(
        handle,
        static_cast<FILE_INFO_BY_HANDLE_CLASS>(kFileCaseSensitiveInfo),
        &info, sizeof(info));
    CloseHandle(handle);
    if (!ok) {
        if (diagnostic) *diagnostic = "case probe: per-directory flag unsupported -> insensitive";
        return CaseSensitivity::CaseInsensitive;
    }
    if ((info.Flags & kCaseSensitiveDir) != 0) {
        if (diagnostic) *diagnostic = "case probe: case-sensitive directory";
        return CaseSensitivity::CaseSensitive;
    }
    if (diagnostic) *diagnostic = "case probe: case-insensitive directory";
    return CaseSensitivity::CaseInsensitive;
#else
    (void)utf8Root;
    if (diagnostic) *diagnostic = "case probe: POSIX case-sensitive";
    return CaseSensitivity::CaseSensitive;
#endif
}

} // namespace

const char* ToString(PathError error) {
    switch (error) {
        case PathError::None: return "None";
        case PathError::Empty: return "Empty";
        case PathError::InvalidCharacter: return "InvalidCharacter";
        case PathError::AbsolutePath: return "AbsolutePath";
        case PathError::UncPath: return "UncPath";
        case PathError::DevicePath: return "DevicePath";
        case PathError::NotAbsolute: return "NotAbsolute";
        case PathError::DotSegment: return "DotSegment";
        case PathError::DotDotSegment: return "DotDotSegment";
        case PathError::RootNotAbsolute: return "RootNotAbsolute";
        case PathError::RootMissing: return "RootMissing";
        case PathError::RootNotDirectory: return "RootNotDirectory";
        case PathError::EscapesRoot: return "EscapesRoot";
        case PathError::LinkNotAllowed: return "LinkNotAllowed";
        case PathError::LinkEscape: return "LinkEscape";
        case PathError::LinkCycle: return "LinkCycle";
    }
    return "Unknown";
}

const char* ToString(CaseSensitivity mode) {
    return mode == CaseSensitivity::CaseSensitive ? "CaseSensitive"
                                                  : "CaseInsensitive";
}

std::string PathKey(std::string_view path, CaseSensitivity mode) {
    return ComparisonKeyFor(mode, path);
}

PathResolution NormalizeProjectRelative(std::string_view input) {    if (input.empty()) return Fail(PathError::Empty);

    std::string value = WithForwardSlashes(input);
    if (value.find('\0') != std::string::npos)
        return Fail(PathError::InvalidCharacter);
    if (HasDevicePrefix(value)) return Fail(PathError::DevicePath);
    if (HasDrivePrefix(value)) return Fail(PathError::AbsolutePath);
    if (value[0] == '/')
        return Fail(value.size() >= 2 && value[1] == '/'
                        ? PathError::UncPath
                        : PathError::AbsolutePath);

    std::string out;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find('/', start);
        if (end == std::string::npos) end = value.size();
        const std::string_view segment(value.data() + start, end - start);
        if (!segment.empty()) {
            if (segment == ".") return Fail(PathError::DotSegment);
            if (segment == "..") return Fail(PathError::DotDotSegment);
            if (!out.empty()) out += '/';
            out.append(segment.data(), segment.size());
        }
        if (end == value.size()) break;
        start = end + 1;
    }
    if (out.empty()) return Fail(PathError::Empty);
    return PathResolution{true, PathError::None, std::move(out)};
}

bool ProjectRoot::Create(std::string_view explicitRoot, ProjectRoot* out,
                         PathError* error, std::string* diagnostic,
                         ProjectRootOptions options) {
    const auto fail = [&](PathError code, std::string message) {
        if (error) *error = code;
        if (diagnostic) *diagnostic = std::move(message);
        if (out) *out = ProjectRoot{};
        return false;
    };

    if (!out) return fail(PathError::Empty, "null project root output");

    if (explicitRoot.empty())
        return fail(PathError::Empty, "explicit project root is empty");

    std::string normalized = WithForwardSlashes(explicitRoot);
    if (normalized.find('\0') != std::string::npos)
        return fail(PathError::InvalidCharacter, "project root contains NUL");
    if (HasDevicePrefix(normalized))
        return fail(PathError::DevicePath,
                    "device/long-path prefix is not supported for ProjectRoot: " +
                        normalized);
    if (!IsAbsoluteForm(normalized))
        return fail(PathError::RootNotAbsolute,
                    "project root must be absolute: " + normalized);
    if (HasDrivePrefix(normalized) &&
        !(normalized.size() >= 3 && normalized[2] == '/'))
        return fail(PathError::RootNotAbsolute,
                    "drive-relative project root is not an absolute path: " +
                        normalized);

    std::error_code ec;
    const fs::path canonical = fs::canonical(PathFromUtf8(normalized), ec);
    if (ec)
        return fail(PathError::RootMissing,
                    "project root is not accessible: " + ec.message());
    if (!fs::is_directory(canonical, ec) || ec)
        return fail(PathError::RootNotDirectory,
                    "project root is not a directory: " + normalized);

    std::string root = StripTrailingSeparator(PathToUtf8(canonical));
    if (root.empty())
        return fail(PathError::RootMissing,
                    "project root canonicalization produced an empty path");

    ProjectRoot result;
    result.m_Root = std::move(root);
    result.m_Options = options;
    std::string caseDiagnostic;
    result.m_Case = ProbeCaseSensitivity(result.m_Root, &caseDiagnostic);

    if (error) *error = PathError::None;
    if (diagnostic) *diagnostic = std::move(caseDiagnostic);
    *out = std::move(result);
    return true;
}

bool ProjectRoot::CheckLinks(std::string_view absolute, PathError* error,
                             std::string* diagnostic) const {
    const PathResolution relative = RelativeOf(m_Case, m_Root, absolute);
    if (!relative.ok) {
        if (error) *error = relative.error;
        return false;
    }

    std::string componentPath = m_Root;
    size_t start = 0;
    while (start <= relative.path.size()) {
        size_t end = relative.path.find('/', start);
        if (end == std::string::npos) end = relative.path.size();
        if (end > start) {
            if (componentPath.back() != '/') componentPath += '/';
            componentPath.append(relative.path, start, end - start);
            if (IsLinkAt(componentPath)) {
                if (!m_Options.allowLinks) {
                    if (error) *error = PathError::LinkNotAllowed;
                    if (diagnostic)
                        *diagnostic = "link component '" + componentPath +
                                      "' is not allowed (allowLinks=false)";
                    return false;
                }
                // allow 模式：链接组件自身必须可解析且解析结果仍在根内。
                // 不能等整条路径存在才检查，否则 `<root>/link/new.txt`
                // （link 指向根外）会以"尾部未创建"被静默放行。
                std::error_code ec;
                const fs::path real =
                    fs::canonical(PathFromUtf8(componentPath), ec);
                if (ec) {
                    if (error) *error = PathError::LinkCycle;
                    if (diagnostic)
                        *diagnostic = "link component '" + componentPath +
                                      "' cannot be resolved: " + ec.message();
                    return false;
                }
                const std::string realPath = PathToUtf8(real);
                if (!ContainsPath(m_Case, m_Root, realPath)) {
                    if (error) *error = PathError::LinkEscape;
                    if (diagnostic)
                        *diagnostic = "link component '" + componentPath +
                                      "' resolves outside project root to '" +
                                      realPath + "'";
                    return false;
                }
            }
        }
        if (end == relative.path.size()) break;
        start = end + 1;
    }
    return true;
}

PathResolution ProjectRoot::ResolveAbsolute(
    std::string_view normalizedAbsolute) const {
    if (!ContainsPath(m_Case, m_Root, normalizedAbsolute))
        return Fail(PathError::EscapesRoot);

    PathError linkError = PathError::None;
    std::string linkDiagnostic;
    if (!CheckLinks(normalizedAbsolute, &linkError, &linkDiagnostic))
        return Fail(linkError, std::move(linkDiagnostic));

    if (m_Options.allowLinks) {
        std::error_code ec;
        const fs::path candidate = PathFromUtf8(normalizedAbsolute);
        if (fs::exists(candidate, ec) && !ec) {
            const fs::path real = fs::canonical(candidate, ec);
            if (ec) {
                return Fail(PathError::LinkCycle,
                            "path '" + std::string(normalizedAbsolute) +
                                "' cannot be resolved: " + ec.message());
            }
            const std::string realPath = PathToUtf8(real);
            if (!ContainsPath(m_Case, m_Root, realPath)) {
                return Fail(PathError::LinkEscape,
                            "path '" + std::string(normalizedAbsolute) +
                                "' resolves outside project root to '" +
                                realPath + "'");
            }
            return PathResolution{true, PathError::None, realPath};
        }
    }
    return PathResolution{true, PathError::None, std::string(normalizedAbsolute)};
}

PathResolution ProjectRoot::Resolve(std::string_view projectRelative) const {
    if (!IsValid()) return Fail(PathError::RootMissing);
    const PathResolution normalized = NormalizeProjectRelative(projectRelative);
    if (!normalized.ok) return normalized;

    std::string absolute = m_Root;
    if (absolute.back() != '/') absolute += '/';
    absolute += normalized.path;
    return ResolveAbsolute(absolute);
}

PathResolution ProjectRoot::ResolveAny(std::string_view path) const {
    if (!IsValid()) return Fail(PathError::RootMissing);
    if (path.empty()) return Fail(PathError::Empty);
    if (path.find('\0') != std::string_view::npos)
        return Fail(PathError::InvalidCharacter);

    const std::string normalized = WithForwardSlashes(path);
    if (HasDevicePrefix(normalized)) return Fail(PathError::DevicePath);
    // UNC 只能作为显式 ProjectRoot（K4.8），任何内容路径形式都结构化拒绝；
    // 不能落到根包含检查被误报成 EscapesRoot（形式非法 ≠ 根外路径）。
    if (StartsWith(normalized, "//")) return Fail(PathError::UncPath);
    if (!IsAbsoluteForm(normalized)) return Resolve(path);
    if (HasDrivePrefix(normalized) &&
        !(normalized.size() >= 3 && normalized[2] == '/'))
        return Fail(PathError::AbsolutePath);

    std::error_code ec;
    const fs::path lexical = PathFromUtf8(normalized).lexically_normal();
    const std::string absolute = StripTrailingSeparator(PathToUtf8(lexical));
    if (absolute.empty()) return Fail(PathError::Empty);
    return ResolveAbsolute(absolute);
}

PathResolution ProjectRoot::ToProjectRelative(std::string_view absolute) const {
    if (!IsValid()) return Fail(PathError::RootMissing);
    if (absolute.empty()) return Fail(PathError::Empty);
    if (absolute.find('\0') != std::string_view::npos)
        return Fail(PathError::InvalidCharacter);

    const std::string normalized = WithForwardSlashes(absolute);
    if (HasDevicePrefix(normalized)) return Fail(PathError::DevicePath);
    if (StartsWith(normalized, "//")) return Fail(PathError::UncPath);
    if (!IsAbsoluteForm(normalized)) return Fail(PathError::NotAbsolute);
    if (HasDrivePrefix(normalized) &&
        !(normalized.size() >= 3 && normalized[2] == '/'))
        return Fail(PathError::NotAbsolute);

    std::error_code ec;
    const std::string lexical = StripTrailingSeparator(
        PathToUtf8(PathFromUtf8(normalized).lexically_normal()));
    if (!ContainsPath(m_Case, m_Root, lexical))
        return Fail(PathError::EscapesRoot);
    return RelativeOf(m_Case, m_Root, lexical);
}

PathResolution ProjectRoot::ToCanonicalProjectPath(
    std::string_view input) const {
    if (!IsValid()) return Fail(PathError::RootMissing);
    const std::string normalized = WithForwardSlashes(input);
    if (HasDevicePrefix(normalized)) return Fail(PathError::DevicePath);
    if (IsAbsoluteForm(normalized)) return ToProjectRelative(input);
    return NormalizeProjectRelative(input);
}

std::string ProjectRoot::ComparisonKey(std::string_view path) const {
    return ComparisonKeyFor(m_Case, path);
}

bool ProjectRoot::KeysEqual(std::string_view a, std::string_view b) const {
    return ComparisonKey(a) == ComparisonKey(b);
}

} // namespace Engine::Content
