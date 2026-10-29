/**
 * @file ProjectPathTest.cpp
 * @brief Content v2 M1-A：K4 路径契约的可执行矩阵（K4.9）
 *
 * 覆盖：规范工程相对路径 / 分隔符 / `.` 与 `..` / 盘符与 UNC / 根越界与
 * 相似前缀 / CWD 变更 / 大小写与 PathKey / 链接策略 / Windows long path /
 * 空路径与根自身。断言直接对应 docs/Roadmap-Decision-Record-2026-10-28.md §3.5。
 */

#include <gtest/gtest.h>
#include "Engine/Core/Content/ProjectPaths.h"
#include "Engine/Core/Content/Utf8Path.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using Engine::Content::CaseSensitivity;
using Engine::Content::NormalizeProjectRelative;
using Engine::Content::PathError;
using Engine::Content::PathResolution;
using Engine::Content::ProjectRoot;
using Engine::Content::ProjectRootOptions;
using Engine::Content::ToString;

namespace {

std::atomic<int> g_Unique{0};

fs::path MakeTempRoot() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("engine_m1a_paths_" + std::to_string(stamp) + "_" +
            std::to_string(g_Unique.fetch_add(1)));
}

std::string AsciiUpper(std::string value) {
    for (char& c : value)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return value;
}

void WriteText(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << body;
}

struct CwdGuard {
    fs::path old = fs::current_path();
    ~CwdGuard() {
        std::error_code ec;
        fs::current_path(old, ec);
    }
};

/// 链接用例在无权限环境中必须显式跳过，而不是把"没建成链接"当成通过。
bool TryCreateDirLink(const fs::path& target, const fs::path& link) {
    std::error_code ec;
    fs::create_directory_symlink(target, link, ec);
    return !ec;
}

} // namespace

class ProjectPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_Temp = MakeTempRoot();
        m_RootPath = m_Temp / "Project";
        ASSERT_TRUE(fs::create_directories(m_RootPath));
        PathError error = PathError::None;
        std::string diagnostic;
        ASSERT_TRUE(ProjectRoot::Create(m_RootPath.generic_string(), &m_Root,
                                        &error, &diagnostic))
            << "root create failed: " << ToString(error) << " (" << diagnostic
            << ")";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_Temp, ec);
    }

    fs::path m_Temp;
    fs::path m_RootPath;
    ProjectRoot m_Root;
};

// ── 合法工程相对路径：唯一规范形式 ──────────────────────────────

TEST_F(ProjectPathTest, LegalRelativePathsHaveUniqueCanonicalForm) {
    const auto canonical = NormalizeProjectRelative("assets/gp01/Main.scene");
    ASSERT_TRUE(canonical.ok);
    EXPECT_EQ(canonical.path, "assets/gp01/Main.scene");

    // 已是规范形式 → 幂等
    const auto again = NormalizeProjectRelative(canonical.path);
    ASSERT_TRUE(again.ok);
    EXPECT_EQ(again.path, canonical.path);

    // 大小写与文件名原样保留（K4.5：比较归比较，不丢作者形态）
    const auto mixed = NormalizeProjectRelative("Assets/GP01/Main.SCENE");
    ASSERT_TRUE(mixed.ok);
    EXPECT_EQ(mixed.path, "Assets/GP01/Main.SCENE");

    // 空格、连字符、非点号后缀合法
    const auto spaced = NormalizeProjectRelative("assets/my textures/tile-01.png");
    ASSERT_TRUE(spaced.ok);
    EXPECT_EQ(spaced.path, "assets/my textures/tile-01.png");
}

// ── 路径分隔符：接受变体但声明恒定 `/` ──────────────────────────

TEST_F(ProjectPathTest, SeparatorVariantsNormalizeToForwardSlash) {
    const char* inputs[] = {
        "assets\\gp01\\Main.scene",
        "assets/gp01\\Main.scene",
        "assets//gp01///Main.scene",
        "assets\\gp01//Main.scene",
        "assets/gp01/Main.scene/",
        "assets\\gp01\\Main.scene\\\\",
    };
    for (const char* input : inputs) {
        const auto r = NormalizeProjectRelative(input);
        ASSERT_TRUE(r.ok) << "input=" << input;
        EXPECT_EQ(r.path, "assets/gp01/Main.scene") << "input=" << input;
        EXPECT_EQ(r.path.find('\\'), std::string::npos) << "input=" << input;
    }

    // 重复分隔符只在折叠后判空：`/`、`///` 不是"空路径"
    EXPECT_EQ(NormalizeProjectRelative("//").error, PathError::UncPath);
    EXPECT_EQ(NormalizeProjectRelative("///").error, PathError::UncPath);
    EXPECT_EQ(NormalizeProjectRelative("/").error, PathError::AbsolutePath);
}

// ── `.` 与 `..`：拒绝，不静默丢弃 ────────────────────────────────

TEST_F(ProjectPathTest, DotSegmentsAreRejectedNotSilentlyDropped) {
    const char* dotInputs[] = {".", "./a", "a/./b", "a/.", "a\\.",
                               "assets/./tex/a.png"};
    for (const char* input : dotInputs) {
        EXPECT_EQ(NormalizeProjectRelative(input).error, PathError::DotSegment)
            << "input=" << input;
    }

    const char* dotdotInputs[] = {"..", "../a", "a/../b", "a/b/..",
                                  "a\\..\\b", "assets/../../x"};
    for (const char* input : dotdotInputs) {
        EXPECT_EQ(NormalizeProjectRelative(input).error,
                  PathError::DotDotSegment)
            << "input=" << input;
    }

    // 空路径与根自身：内容路径必须有实体，根本身不是合法内容路径
    EXPECT_EQ(NormalizeProjectRelative("").error, PathError::Empty);
    EXPECT_EQ(m_Root.Resolve("").error, PathError::Empty);
    EXPECT_EQ(m_Root.ResolveAny(m_Root.Path()).error, PathError::Empty);
    EXPECT_EQ(m_Root.ToProjectRelative(m_Root.Path()).error, PathError::Empty);
}

// ── 盘符与 UNC：不作为工程相对路径 ──────────────────────────────

TEST_F(ProjectPathTest, DriveAndUncFormsAreRejected) {
    const char* driveInputs[] = {"C:/assets/a.png", "C:assets/a.png",
                                 "c:\\assets\\a.png", "Z:"};
    for (const char* input : driveInputs) {
        EXPECT_EQ(NormalizeProjectRelative(input).error,
                  PathError::AbsolutePath)
            << "input=" << input;
        EXPECT_EQ(m_Root.Resolve(input).error, PathError::AbsolutePath)
            << "input=" << input;
    }

    const char* uncInputs[] = {"//server/share/a.png",
                               "\\\\server\\share\\a.png", "//server",
                               "\\\\server"};
    for (const char* input : uncInputs) {
        EXPECT_EQ(NormalizeProjectRelative(input).error, PathError::UncPath)
            << "input=" << input;
        EXPECT_EQ(m_Root.Resolve(input).error, PathError::UncPath)
            << "input=" << input;
        // 绝对入口同样按形式拒绝：UNC 只能作为显式 ProjectRoot（K4.8），
        // 不得被根包含检查降级成 EscapesRoot。
        EXPECT_EQ(m_Root.ResolveAny(input).error, PathError::UncPath)
            << "input=" << input;
        EXPECT_EQ(m_Root.ToProjectRelative(input).error, PathError::UncPath)
            << "input=" << input;
        EXPECT_EQ(m_Root.ToCanonicalProjectPath(input).error, PathError::UncPath)
            << "input=" << input;
    }
}

// ── 设备 / long path 前缀：M1-A 明确拒绝 ────────────────────────

TEST_F(ProjectPathTest, DeviceAndLongPathPrefixesAreRejected) {
    const char* deviceInputs[] = {
        "\\\\?\\C:\\assets\\a.png",
        "//?/C:/assets/a.png",
        "\\\\.\\pipe\\asset",
        "//./pipe/asset",
        "\\\\?\\UNC\\server\\share\\a.png",
    };
    for (const char* input : deviceInputs) {
        EXPECT_EQ(NormalizeProjectRelative(input).error, PathError::DevicePath)
            << "input=" << input;
        EXPECT_EQ(m_Root.Resolve(input).error, PathError::DevicePath)
            << "input=" << input;
        EXPECT_EQ(m_Root.ResolveAny(input).error, PathError::DevicePath)
            << "input=" << input;
        EXPECT_EQ(m_Root.ToProjectRelative(input).error, PathError::DevicePath)
            << "input=" << input;
        EXPECT_EQ(m_Root.ToCanonicalProjectPath(input).error,
                  PathError::DevicePath)
            << "input=" << input;
    }

    // 显式 ProjectRoot 同样拒绝：不截断前缀后"尽量打开"
    PathError error = PathError::None;
    std::string diagnostic;
    ProjectRoot rejected;
    EXPECT_FALSE(ProjectRoot::Create("\\\\?\\C:\\", &rejected, &error,
                                     &diagnostic));
    EXPECT_EQ(error, PathError::DevicePath);
    EXPECT_FALSE(diagnostic.empty());
    EXPECT_FALSE(rejected.IsValid());
}

// ── 显式 ProjectRoot：绝对、已存在、目录；绝不回退 CWD ──────────

TEST_F(ProjectPathTest, RootCreationRequiresExplicitAbsoluteDirectory) {
    PathError error = PathError::None;
    std::string diagnostic;
    ProjectRoot out;

    EXPECT_FALSE(ProjectRoot::Create("", &out, &error, &diagnostic));
    EXPECT_EQ(error, PathError::Empty);

    EXPECT_FALSE(ProjectRoot::Create("relative/root", &out, &error,
                                     &diagnostic));
    EXPECT_EQ(error, PathError::RootNotAbsolute);

    EXPECT_FALSE(ProjectRoot::Create("C:relative", &out, &error, &diagnostic));
    EXPECT_EQ(error, PathError::RootNotAbsolute);

    const fs::path missing = m_Temp / "does-not-exist";
    EXPECT_FALSE(ProjectRoot::Create(missing.generic_string(), &out, &error,
                                     &diagnostic));
    EXPECT_EQ(error, PathError::RootMissing);

    const fs::path file = m_Temp / "file.txt";
    WriteText(file, "x");
    EXPECT_FALSE(ProjectRoot::Create(file.generic_string(), &out, &error,
                                     &diagnostic));
    EXPECT_EQ(error, PathError::RootNotDirectory);

    // 合法根：混合分隔符 + 尾分隔符都归一
    const std::string trailing =
        m_RootPath.generic_string() + "//";
    ASSERT_TRUE(ProjectRoot::Create(trailing, &out, &error, &diagnostic))
        << ToString(error) << " (" << diagnostic << ")";
    EXPECT_FALSE(out.Path().empty());
    EXPECT_EQ(out.Path().find('\\'), std::string::npos);
    EXPECT_NE(out.Path().back(), '/');
    EXPECT_EQ(out.Path(), m_Root.Path());
    EXPECT_TRUE(out.IsValid());
    EXPECT_FALSE(std::string(ToString(out.CaseMode())).empty());
}

// ── Resolve：根锚定；尾部无需已存在；越界/绝对/UNC 拒绝 ────────

TEST_F(ProjectPathTest, ResolveAnchorsToRootWithoutRequiringExistingTail) {
    const auto r = m_Root.Resolve("assets/gp01/not-created-yet.png");
    ASSERT_TRUE(r.ok) << ToString(r.error);
    EXPECT_EQ(r.path, m_Root.Path() + "/assets/gp01/not-created-yet.png");

    const auto variant = m_Root.Resolve("assets\\gp01\\not-created-yet.png");
    ASSERT_TRUE(variant.ok);
    EXPECT_EQ(variant.path, r.path);

    EXPECT_EQ(m_Root.Resolve("..").error, PathError::DotDotSegment);
    EXPECT_EQ(m_Root.Resolve("../outside.png").error,
              PathError::DotDotSegment);
    EXPECT_EQ(m_Root.Resolve("/etc/passwd").error, PathError::AbsolutePath);

    ProjectRoot unset;
    EXPECT_FALSE(unset.IsValid());
    EXPECT_EQ(unset.Resolve("assets/a.png").error, PathError::RootMissing);
    EXPECT_EQ(unset.ResolveAny("assets/a.png").error, PathError::RootMissing);
}

// ── ResolveAny：legacy 绝对路径只在根内兼容 ────────────────────

TEST_F(ProjectPathTest, ResolveAnyAcceptsOnlyAbsolutePathsInsideRoot) {
    const std::string inside = m_Root.Path() + "/assets/tex/a.png";
    const auto inRoot = m_Root.ResolveAny(inside);
    ASSERT_TRUE(inRoot.ok) << ToString(inRoot.error);
    EXPECT_EQ(inRoot.path, inside);

    // 绝对输入中的 `..` 先做词法归一，再判包含：inside 的往返仍成立
    const auto normalized = m_Root.ResolveAny(m_Root.Path() +
                                              "/assets/../tex/a.png");
    ASSERT_TRUE(normalized.ok);
    EXPECT_EQ(normalized.path, m_Root.Path() + "/tex/a.png");

    // 词法归一后越界 → 拒绝
    const auto escape =
        m_Root.ResolveAny(m_Root.Path() + "/../ProjectOther/x.png");
    EXPECT_EQ(escape.error, PathError::EscapesRoot);

    // 相对输入中的 `..` 不经过词法归一，直接结构化拒绝
    EXPECT_EQ(m_Root.ResolveAny("assets/../../x.png").error,
              PathError::DotDotSegment);

    // 根内绝对路径的重复分隔符
    const auto dup = m_Root.ResolveAny(m_Root.Path() + "//assets//a.png");
    ASSERT_TRUE(dup.ok);
    EXPECT_EQ(dup.path, m_Root.Path() + "/assets/a.png");
}

// ── 相似前缀：`Project` 不得接受 `ProjectOther` ────────────────

TEST_F(ProjectPathTest, SimilarPrefixSiblingsAreOutsideTheRoot) {
    const fs::path sibling = m_Temp / "ProjectOther";
    ASSERT_TRUE(fs::create_directories(sibling));

    const auto viaResolveAny =
        m_Root.ResolveAny((sibling / "assets/a.png").generic_string());
    EXPECT_EQ(viaResolveAny.error, PathError::EscapesRoot);

    const std::string withSeparator =
        fs::path(m_Root.Path()).generic_string() + "/nested";
    EXPECT_FALSE(m_Root.ToProjectRelative(sibling.generic_string()).ok);

    // 根路径前缀 + 非分隔符字符 = 越界（不是 StartsWith 的裸比较）
    EXPECT_EQ(m_Root.ToProjectRelative(m_Root.Path() + "Other/a.png").error,
              PathError::EscapesRoot);
    EXPECT_EQ(m_Root.ResolveAny(m_Root.Path() + "Other/a.png").error,
              PathError::EscapesRoot);

    // 根内的真实子路径正常
    const auto ok = m_Root.ToProjectRelative(withSeparator);
    ASSERT_TRUE(ok.ok);
    EXPECT_EQ(ok.path, "nested");
}

// ── ToProjectRelative / ToCanonicalProjectPath：往返与拒绝 ──────

TEST_F(ProjectPathTest, ProjectRelativeConversionRoundTrips) {
    const auto relative =
        m_Root.ToProjectRelative(m_Root.Path() + "/assets/tex/a.png");
    ASSERT_TRUE(relative.ok);
    EXPECT_EQ(relative.path, "assets/tex/a.png");

    const auto backslash = m_Root.ToProjectRelative(
        m_Root.Path() + "\\assets\\tex\\a.png");
    ASSERT_TRUE(backslash.ok);
    EXPECT_EQ(backslash.path, "assets/tex/a.png");

    EXPECT_EQ(m_Root.ToProjectRelative("assets/a.png").error,
              PathError::NotAbsolute);
    EXPECT_EQ(m_Root.ToProjectRelative("C:assets/a.png").error,
              PathError::NotAbsolute);

    // 导入/清单归一：相对与根内绝对得到同一规范形式
    const auto fromRelative =
        m_Root.ToCanonicalProjectPath("assets\\tex\\a.png");
    ASSERT_TRUE(fromRelative.ok);
    EXPECT_EQ(fromRelative.path, "assets/tex/a.png");
    const auto fromAbsolute =
        m_Root.ToCanonicalProjectPath(m_Root.Path() + "/assets/tex/a.png");
    ASSERT_TRUE(fromAbsolute.ok);
    EXPECT_EQ(fromAbsolute.path, fromRelative.path);

    EXPECT_EQ(m_Root.ToCanonicalProjectPath("../a.png").error,
              PathError::DotDotSegment);
    EXPECT_EQ(
        m_Root.ToCanonicalProjectPath(m_Temp.generic_string() + "/a.png").error,
        PathError::EscapesRoot);
}

// ── 大小写与 PathKey：按卷能力探测，不按操作系统猜测 ────────────

TEST_F(ProjectPathTest, PathKeysFollowProbedCaseCapability) {
    // 分隔符变体永远共享同一个比较键
    EXPECT_TRUE(m_Root.KeysEqual("assets\\tex\\a.png", "assets//tex/a.png/"));
    EXPECT_EQ(m_Root.ComparisonKey("assets\\tex\\a.png"),
              m_Root.ComparisonKey("assets/tex/a.png"));
    EXPECT_FALSE(m_Root.KeysEqual("assets/a.png", "assets/b.png"));

    const std::string lower = "assets/tex/atlas.png";
    const std::string upper = "ASSETS/TEX/ATLAS.PNG";
    if (m_Root.CaseMode() == CaseSensitivity::CaseInsensitive) {
        EXPECT_TRUE(m_Root.KeysEqual(lower, upper));
        EXPECT_EQ(m_Root.ComparisonKey(upper),
                  m_Root.ComparisonKey(lower));
        // 包含检查同样按探测到的大小写能力比较
        const fs::path upperRoot = AsciiUpper(m_Root.Path());
        const auto resolved =
            m_Root.ResolveAny(upperRoot.generic_string() + "/assets/a.png");
        EXPECT_TRUE(resolved.ok) << ToString(resolved.error);
    } else {
        EXPECT_FALSE(m_Root.KeysEqual(lower, upper));
        EXPECT_NE(m_Root.ComparisonKey(upper),
                  m_Root.ComparisonKey(lower));
        const fs::path upperRoot = AsciiUpper(m_Root.Path());
        if (upperRoot.generic_string() != m_Root.Path()) {
            const auto resolved =
                m_Root.ResolveAny(upperRoot.generic_string() + "/assets/a.png");
            EXPECT_EQ(resolved.error, PathError::EscapesRoot);
        }
    }
}

// ── PathKey 保留根命名空间：UNC 与当前盘 rooted 不可折叠 ─────────

TEST_F(ProjectPathTest, PathKeysKeepUncAndRootedNamespacesDistinct) {
    // UNC 允许作为显式 ProjectRoot（K4.8）。若比较键把前导 `//` 折叠成 `/`，
    // UNC 根会把当前盘的 `/server/share/...` 误判为根内路径。
    EXPECT_FALSE(m_Root.KeysEqual("//server/share/a.png",
                                  "/server/share/a.png"));
    EXPECT_FALSE(m_Root.KeysEqual("//server/share/a.png",
                                  "server/share/a.png"));
    EXPECT_NE(m_Root.ComparisonKey("//server/share/a.png"),
              m_Root.ComparisonKey("/server/share/a.png"));

    // 同一命名空间内的分隔符变体仍然折叠
    EXPECT_TRUE(m_Root.KeysEqual("//server\\share\\a.png",
                                 "//server//share/a.png/"));
    EXPECT_TRUE(m_Root.KeysEqual("/server\\share\\a.png",
                                 "/server/share/a.png"));
}

// ── 非 ASCII 大小写：PathKey 是 ordinal-ignore-case，不是 ASCII-only ──

TEST_F(ProjectPathTest, PathKeysFoldNonAsciiCaseByVolumeCapability) {
    // `Ä`/`ä`（Latin-1）与 `Тест`/`тест`（西里尔）。只折 ASCII 会让 K4.5 的
    // 重复检测在非 ASCII 路径上留洞；卷能力敏感时大小写敏感卷必须区分它们。
    const std::string upperLatin = "assets/\xC3\x84.png";          // Ä
    const std::string lowerLatin = "assets/\xC3\xA4.png";          // ä
    const std::string upperCyrillic =
        "\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82/a.png";                  // Тест
    const std::string lowerCyrillic =
        "\xD1\x82\xD0\xB5\xD1\x81\xD1\x82/a.png";                  // тест

    if (m_Root.CaseMode() == CaseSensitivity::CaseInsensitive) {
        EXPECT_TRUE(m_Root.KeysEqual(upperLatin, lowerLatin));
        EXPECT_TRUE(m_Root.KeysEqual(upperCyrillic, lowerCyrillic));
        EXPECT_EQ(m_Root.ComparisonKey(lowerLatin), "ASSETS/Ä.PNG");
        // 只折大小写，不把不同字母合并成同一键
        EXPECT_FALSE(m_Root.KeysEqual(
            upperCyrillic, "\xD0\xA2\xD0\xB5\xD1\x81\xD0\xB3/a.png"));
        // 非法 UTF-8 不得崩溃，折叠保持确定性（回退为按字节 ASCII 折叠）
        EXPECT_EQ(m_Root.ComparisonKey("\xFF\x41"),
                  m_Root.ComparisonKey("\xFF\x61"));
    } else {
        EXPECT_FALSE(m_Root.KeysEqual(upperLatin, lowerLatin));
        EXPECT_FALSE(m_Root.KeysEqual(upperCyrillic, lowerCyrillic));
        EXPECT_EQ(m_Root.ComparisonKey(upperLatin), upperLatin);
        EXPECT_EQ(m_Root.ComparisonKey("\xFF\x41"), "\xFF\x41");
    }
}

// ── 非 ASCII 路径：K4.2 的 UTF-8 必须穿过 OS 边界，而不是按 ACP 解释 ──

TEST_F(ProjectPathTest, NonAsciiPathsUseUtf8AcrossOsBoundary) {
    const std::string directory = "资源Ä";
    const std::string file = "贴图.png";
    const fs::path assetDir = m_RootPath / Engine::Content::PathFromUtf8(directory);
    ASSERT_TRUE(fs::create_directories(assetDir));
    WriteText(assetDir / Engine::Content::PathFromUtf8(file), "pixels");

    const std::string utf8Root = Engine::Content::PathToUtf8(m_RootPath);
    ProjectRoot root;
    PathError error = PathError::None;
    std::string diagnostic;
    ASSERT_TRUE(ProjectRoot::Create(utf8Root, &root, &error, &diagnostic))
        << ToString(error) << " " << diagnostic;
    EXPECT_EQ(root.Path(), utf8Root);

    const std::string relative = directory + "/" + file;
    const auto resolved = root.Resolve(relative);
    ASSERT_TRUE(resolved.ok) << ToString(resolved.error);
    EXPECT_EQ(resolved.path, utf8Root + "/" + relative);
    EXPECT_TRUE(fs::exists(Engine::Content::PathFromUtf8(resolved.path)));

    const auto roundTrip = root.ToProjectRelative(resolved.path);
    ASSERT_TRUE(roundTrip.ok) << ToString(roundTrip.error);
    EXPECT_EQ(roundTrip.path, relative);
}

// ── CWD 变更：解析结果不漂移；相对根不按 CWD 猜测 ───────────────

TEST_F(ProjectPathTest, ResolutionDoesNotDriftWhenCwdChanges) {
    const auto before = m_Root.Resolve("assets/gp01/Main.scene");
    ASSERT_TRUE(before.ok);

    const fs::path elsewhere = m_Temp / "elsewhere";
    ASSERT_TRUE(fs::create_directories(elsewhere));

    CwdGuard guard;
    fs::current_path(elsewhere);

    const auto after = m_Root.Resolve("assets/gp01/Main.scene");
    ASSERT_TRUE(after.ok);
    EXPECT_EQ(after.path, before.path);

    // CWD 里有同名相对目录也不能让相对 ProjectRoot 变合法
    std::error_code ec;
    fs::create_directories(elsewhere / "relative" / "root");
    PathError error = PathError::None;
    std::string diagnostic;
    ProjectRoot cwdRoot;
    EXPECT_FALSE(ProjectRoot::Create("relative/root", &cwdRoot, &error,
                                     &diagnostic));
    EXPECT_EQ(error, PathError::RootNotAbsolute);

    // 在 CWD 变更期间新建根，结果仍以显式根为准
    ProjectRoot second;
    ASSERT_TRUE(ProjectRoot::Create(m_Root.Path(), &second, &error,
                                    &diagnostic));
    const auto secondResolve = second.Resolve("assets/gp01/Main.scene");
    ASSERT_TRUE(secondResolve.ok);
    EXPECT_EQ(secondResolve.path, before.path);
}

// ── 符号链接 / reparse point：默认拒绝；显式允许时 real path 复查 ──

TEST_F(ProjectPathTest, LinksAreRejectedByDefaultAndRecheckedWhenAllowed) {
    const fs::path real = m_RootPath / "real";
    const fs::path outside = m_Temp / "Outside";
    ASSERT_TRUE(fs::create_directories(real));
    ASSERT_TRUE(fs::create_directories(outside));
    WriteText(real / "file.txt", "inside");
    WriteText(outside / "secret.txt", "outside");

    if (!TryCreateDirLink(real, m_RootPath / "link") ||
        !TryCreateDirLink(outside, m_RootPath / "escape") ||
        !TryCreateDirLink(m_RootPath / "missing", m_RootPath / "dangling") ||
        !TryCreateDirLink(m_RootPath / "loop", m_RootPath / "loop")) {
        GTEST_SKIP() << "symlink creation unavailable in this environment";
    }

    // 默认策略：任何经过链接的路径都拒绝（含尾部尚未创建的路径）
    EXPECT_EQ(m_Root.Resolve("link/file.txt").error,
              PathError::LinkNotAllowed);
    EXPECT_EQ(m_Root.Resolve("link/new.txt").error,
              PathError::LinkNotAllowed);
    EXPECT_EQ(m_Root.ResolveAny(m_Root.Path() + "/link/file.txt").error,
              PathError::LinkNotAllowed);

    PathError error = PathError::None;
    std::string diagnostic;
    ProjectRoot allow;
    ASSERT_TRUE(ProjectRoot::Create(m_RootPath.generic_string(), &allow,
                                    &error, &diagnostic,
                                    ProjectRootOptions{true}))
        << ToString(error) << " (" << diagnostic << ")";

    // 允许：根内链接解析为 real path
    std::error_code canonicalEc;
    const fs::path realFile = fs::canonical(real / "file.txt", canonicalEc);
    ASSERT_FALSE(canonicalEc) << canonicalEc.message();
    const auto inside = allow.Resolve("link/file.txt");
    ASSERT_TRUE(inside.ok) << ToString(inside.error);
    EXPECT_EQ(inside.path, realFile.generic_string());

    // 允许：尾部未创建但链接目标仍在根内 → 放行
    const auto pending = allow.Resolve("link/new.txt");
    ASSERT_TRUE(pending.ok) << ToString(pending.error);

    // 允许：链接指向根外 → LinkEscape（无论尾部是否存在）
    const fs::path outsideReal = fs::canonical(outside, canonicalEc);
    ASSERT_FALSE(canonicalEc) << canonicalEc.message();
    const auto escaped = allow.Resolve("escape/secret.txt");
    EXPECT_EQ(escaped.error, PathError::LinkEscape);
    EXPECT_NE(escaped.diagnostic.find(outsideReal.generic_string()),
              std::string::npos)
        << "diagnostic must carry the real path: " << escaped.diagnostic;

    const auto escapePending = allow.Resolve("escape/new.txt");
    EXPECT_EQ(escapePending.error, PathError::LinkEscape);
    EXPECT_FALSE(escapePending.diagnostic.empty());

    // 允许：悬空链接与环都不可解析 → LinkCycle（带组件诊断）
    const auto dangling = allow.Resolve("dangling/x.txt");
    EXPECT_EQ(dangling.error, PathError::LinkCycle);
    EXPECT_NE(dangling.diagnostic.find("dangling"), std::string::npos)
        << dangling.diagnostic;

    const auto cycle = allow.Resolve("loop/x.txt");
    EXPECT_EQ(cycle.error, PathError::LinkCycle);
    EXPECT_NE(cycle.diagnostic.find("loop"), std::string::npos)
        << cycle.diagnostic;

    // 默认根下的悬空/环也只是 LinkNotAllowed，不会因目标不存在而放行
    EXPECT_EQ(m_Root.Resolve("dangling/x.txt").error,
              PathError::LinkNotAllowed);
    EXPECT_EQ(m_Root.Resolve("loop/x.txt").error, PathError::LinkNotAllowed);
}

// ── 结构一致性：成功结果不携带错误/诊断 ─────────────────────────

TEST_F(ProjectPathTest, SuccessfulResolutionsCarryNoErrorState) {
    const auto r = m_Root.Resolve("assets/a.png");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.error, PathError::None);
    EXPECT_TRUE(r.diagnostic.empty());

    const auto failed = m_Root.Resolve("../a.png");
    EXPECT_FALSE(failed.ok);
    EXPECT_NE(failed.error, PathError::None);
    EXPECT_TRUE(failed.path.empty());
}
