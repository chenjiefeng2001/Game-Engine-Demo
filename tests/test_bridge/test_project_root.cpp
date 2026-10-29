/**
 * @file test_project_root.cpp
 * @brief Content v2 M1-A：rooted open 与 legacy 入口的 CWD 锚定契约
 *
 * 生产路径 `EditorSession_OpenProjectRooted` / `EngineHost_OpenProjectRooted`
 * 必须完全不依赖进程 CWD；legacy `OpenProject` 只能在调用边界把 CWD 一次性
 * 捕获为显式 legacy root（K4.4），之后的读写都以该根为准。
 *
 * 本文件走真实 DLL 导出，使用 GP01 工程的临时副本，不污染 repo 资产。
 */

#include <gtest/gtest.h>
#include "editor_bridge/capi.h"

#include <Engine/Core/Content/Utf8Path.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr const char* kManifest = "assets/gp01/manifest.json";
constexpr const char* kScene = "assets/gp01/Main.scene";

std::atomic<int> g_Unique{0};

/// 进程启动时的 CWD = CTest 固定的仓库根。用于定位只读源资产，
/// 与被测的路径解析无关（测试内会显式切换 CWD）。
const fs::path& RepoRoot() {
    static const fs::path root = fs::current_path();
    return root;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string SessionError(EditorSessionHandle session) {
    char buf[1024] = {};
    EditorSession_GetLastError(session, buf, sizeof(buf));
    return buf;
}

/// 创建隔离的 scratch 工程副本：<scratch>/assets/gp01 + <scratch>/empty-cwd。
/// 供 fixture 与需要独占 GLFW 持久生命周期的 EngineHost 用例共用。
bool PrepareScratch(fs::path* scratch, fs::path* rootPath, std::string* root,
                    std::string* error) {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    *scratch = fs::temp_directory_path() /
               ("engine_m1a_bridge_" + std::to_string(stamp) + "_" +
                std::to_string(g_Unique.fetch_add(1)));

    std::error_code ec;
    fs::create_directories(*scratch / "assets", ec);
    if (ec) {
        *error = ec.message();
        return false;
    }
    fs::copy(RepoRoot() / "assets" / "gp01", *scratch / "assets" / "gp01",
             fs::copy_options::recursive |
                 fs::copy_options::overwrite_existing,
             ec);
    if (ec) {
        *error = ec.message();
        return false;
    }
    fs::create_directories(*scratch / "empty-cwd", ec);
    if (ec) {
        *error = ec.message();
        return false;
    }
    *rootPath = fs::canonical(*scratch, ec);
    if (ec) {
        *error = ec.message();
        return false;
    }
    *root = rootPath->generic_string();
    return true;
}

/// 还原 CWD 并清理 scratch（与 fixture TearDown 的顺序一致）。
class ScratchProjectGuard {
public:
    ScratchProjectGuard(fs::path oldCwd, fs::path scratch)
        : m_OldCwd(std::move(oldCwd)), m_Scratch(std::move(scratch)) {}
    ~ScratchProjectGuard() {
        std::error_code ec;
        fs::current_path(m_OldCwd, ec);
        fs::remove_all(m_Scratch, ec);
    }
    ScratchProjectGuard(const ScratchProjectGuard&) = delete;
    ScratchProjectGuard& operator=(const ScratchProjectGuard&) = delete;

private:
    fs::path m_OldCwd;
    fs::path m_Scratch;
};

} // namespace

class ProjectRootBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
        m_OldCwd = fs::current_path();

        std::string error;
        ASSERT_TRUE(PrepareScratch(&m_Scratch, &m_RootPath, &m_Root, &error))
            << error;

        m_Session = EditorSession_Create();
        ASSERT_NE(m_Session, nullptr);
    }

    void TearDown() override {
        if (m_Session) EditorSession_Destroy(m_Session);
        std::error_code ec;
        fs::current_path(m_OldCwd, ec);
        fs::remove_all(m_Scratch, ec);
    }

    void SetCwd(const fs::path& path) {
        std::error_code ec;
        fs::current_path(path, ec);
        ASSERT_FALSE(ec) << "cannot enter " << path << ": " << ec.message();
    }

    fs::path m_OldCwd;
    fs::path m_Scratch;
    fs::path m_RootPath;
    std::string m_Root;
    EditorSessionHandle m_Session = nullptr;
};

// ── 生产入口：rooted open 不读取进程 CWD ────────────────────────

TEST_F(ProjectRootBridgeTest, RootedOpenIsIndependentOfProcessCwd) {
    SetCwd(m_Scratch / "empty-cwd");   // 该目录没有任何 assets/

    ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              1)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
    EXPECT_GE(EditorSession_GetAssetCount(m_Session), 33);

    char warn[512] = {};
    EXPECT_EQ(EditorSession_GetWarnings(m_Session, warn, sizeof(warn)), 0);
    EXPECT_STREQ(warn, "") << "clean rooted open must not warn";

    // 写路径同样锚定 root：CWD 仍为空目录，SaveProject 必须落回 scratch。
    const fs::path repoScene = RepoRoot() / kScene;
    const auto repoBefore = fs::last_write_time(repoScene);
    const fs::path scratchScene = m_RootPath / kScene;
    const auto scratchBefore = fs::last_write_time(scratchScene);

    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "RootedProbe"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);

    EXPECT_GT(fs::last_write_time(scratchScene), scratchBefore);
    EXPECT_EQ(fs::last_write_time(repoScene), repoBefore)
        << "SaveProject must never write through the process CWD";
    EXPECT_FALSE(fs::exists(m_Scratch / "empty-cwd" / "assets"))
        << "no directory tree may be created beside the CWD";
}

TEST_F(ProjectRootBridgeTest, RootedOpenRejectsEscapesAndInvalidRoots) {
    SetCwd(m_Scratch / "empty-cwd");

    // 相对根不被 CWD 猜测为合法根
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, "assets", kManifest,
                                              kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("RootNotAbsolute"), std::string::npos)
        << SessionError(m_Session);

    EXPECT_EQ(EditorSession_OpenProjectRooted(
                  m_Session, (m_Scratch / "no-root").generic_string().c_str(),
                  kManifest, kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("RootMissing"), std::string::npos);

    // `..` 在任何位置都是结构化拒绝，不做静默归一后放行
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              "../manifest.json", kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("DotDotSegment"),
              std::string::npos)
        << SessionError(m_Session);

    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, "../Main.scene"),
              0);
    const std::string sceneError = SessionError(m_Session);
    EXPECT_NE(sceneError.find("scene path"), std::string::npos) << sceneError;
    EXPECT_NE(sceneError.find("DotDotSegment"), std::string::npos) << sceneError;

    // 根外绝对路径（repo 实际 manifest）不得被 legacy 语义顺手接受
    const std::string outsideManifest =
        (RepoRoot() / kManifest).generic_string();
    EXPECT_EQ(EditorSession_OpenProjectRooted(
                  m_Session, m_Root.c_str(), outsideManifest.c_str(), kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("EscapesRoot"), std::string::npos)
        << SessionError(m_Session);

    // UNC / 设备前缀（含 `\\?\` long path）显式拒绝
    EXPECT_EQ(EditorSession_OpenProjectRooted(
                  m_Session, m_Root.c_str(), "//server/share/manifest.json",
                  kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("UncPath"), std::string::npos)
        << SessionError(m_Session);

    EXPECT_EQ(EditorSession_OpenProjectRooted(
                  m_Session, m_Root.c_str(), "\\\\?\\C:\\manifest.json",
                  kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("DevicePath"), std::string::npos)
        << SessionError(m_Session);

    // 失败的打开不得留下半成品场景（无失败后部分提交）
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), -1);
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "Ghost"), -1);

    // 同一 session 随后仍可正常打开
    ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              1)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
}

TEST_F(ProjectRootBridgeTest, NullArgumentsRejectedOnRootedExports) {
    EXPECT_EQ(EditorSession_OpenProjectRooted(nullptr, m_Root.c_str(),
                                              kManifest, kScene),
              0);
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, nullptr, kManifest,
                                              kScene),
              0);
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              nullptr, kScene),
              0);
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, nullptr),
              0);

    RuntimeResult result = {};
    EXPECT_EQ(EngineHost_OpenProjectRooted(nullptr, m_Root.c_str(), kManifest,
                                           kScene, &result),
              -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SESSION_FAILED);
    EXPECT_EQ(result.stage, ENGINE_HOST_STAGE_OPEN_PROJECT);
}

// ── Legacy 入口：CWD 在边界一次性捕获（K4.4） ──────────────────

TEST_F(ProjectRootBridgeTest, LegacyOpenCapturesCwdOnceAsExplicitRoot) {
    // CWD 是空目录：legacy 相对路径只在该根内解析，不回退到 repo 或其他位置
    SetCwd(m_Scratch / "empty-cwd");
    EXPECT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 0);
    EXPECT_NE(SessionError(m_Session).find("manifest load failed"),
              std::string::npos)
        << SessionError(m_Session);

    // legacy 的绝对路径必须在 CWD 根内：根外 = 结构化失败
    const std::string outsideManifest =
        (RepoRoot() / kManifest).generic_string();
    EXPECT_EQ(EditorSession_OpenProject(m_Session, outsideManifest.c_str(),
                                        kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("EscapesRoot"), std::string::npos)
        << SessionError(m_Session);

    // CWD 落到 scratch 后 legacy 相对打开成功
    SetCwd(m_Scratch);
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);

    // 打开后 CWD 再切换：会话已锚定 scratch，读写不漂移
    SetCwd(m_Scratch / "empty-cwd");
    const fs::path repoScene = RepoRoot() / kScene;
    const auto repoBefore = fs::last_write_time(repoScene);
    const fs::path scratchScene = m_RootPath / kScene;
    const auto scratchBefore = fs::last_write_time(scratchScene);

    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "LegacyProbe"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);

    EXPECT_GT(fs::last_write_time(scratchScene), scratchBefore);
    EXPECT_EQ(fs::last_write_time(repoScene), repoBefore);
}

// ── 导入 / 脚本 IO / 改名：全部以 ProjectRoot 为锚 ─────────────

TEST_F(ProjectRootBridgeTest, ContentOperationsStayAnchoredToProjectRoot) {
    SetCwd(m_Scratch / "empty-cwd");
    ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              1)
        << SessionError(m_Session);
    const int32_t assetsBefore = EditorSession_GetAssetCount(m_Session);
    ASSERT_GT(assetsBefore, 0);

    // 源文件放到 scratch 根内；CWD 在别处
    std::error_code ec;
    fs::copy_file(RepoRoot() / "assets/gp01/tex/pad.png",
                  m_RootPath / "assets/gp01/tex/rooted_new.png",
                  fs::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec) << ec.message();

    // 分隔符变体与规范形式指向同一资产（幂等 → 同一索引）
    const int32_t imported =
        EditorSession_ImportAsset(m_Session, "assets\\gp01\\tex\\rooted_new.png",
                                  0);
    ASSERT_GE(imported, 0) << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), assetsBefore + 1);
    EXPECT_EQ(EditorSession_ImportAsset(m_Session,
                                        "assets/gp01/tex/rooted_new.png", 0),
              imported);

    char path[512] = {};
    ASSERT_GE(EditorSession_GetAssetPath(m_Session, imported, path,
                                         sizeof(path)),
              0);
    EXPECT_STREQ(path, "assets/gp01/tex/rooted_new.png");

    // 越界导入结构化拒绝
    EXPECT_EQ(EditorSession_ImportAsset(m_Session, "../evil.png", 0), -1);
    EXPECT_NE(SessionError(m_Session).find("DotDotSegment"), std::string::npos)
        << SessionError(m_Session);
    // M1-E：绝对源路径由统一导入服务作为“外部源拷贝”处理，不再属于根越界；
    // 目标路径的越界（相对路径）仍由上面的用例覆盖。

    // 改名：只改 basename，物理文件在 scratch 内移动，GUID 索引不变
    ASSERT_EQ(EditorSession_RenameAsset(m_Session, imported, "rooted_renamed"),
              0)
        << SessionError(m_Session);
    ASSERT_GE(EditorSession_GetAssetPath(m_Session, imported, path,
                                         sizeof(path)),
              0);
    EXPECT_STREQ(path, "assets/gp01/tex/rooted_renamed.png");
    EXPECT_TRUE(fs::exists(m_RootPath / "assets/gp01/tex/rooted_renamed.png"));
    EXPECT_FALSE(fs::exists(m_RootPath / "assets/gp01/tex/rooted_new.png"));
    EXPECT_FALSE(fs::exists(m_Scratch / "empty-cwd" / "assets"));

    // 脚本读：从 scratch 副本读取，而不是 CWD 或 repo
    int32_t scriptIndex = -1;
    for (int32_t i = 0; i < EditorSession_GetAssetCount(m_Session); ++i) {
        if (EditorSession_GetAssetType(m_Session, i) == 1) {
            scriptIndex = i;
            break;
        }
    }
    ASSERT_GE(scriptIndex, 0);
    ASSERT_GE(EditorSession_GetAssetPath(m_Session, scriptIndex, path,
                                         sizeof(path)),
              0);
    const std::string scriptRelative = path;
    const fs::path scratchScript = m_RootPath / scriptRelative;
    const fs::path repoScript = RepoRoot() / scriptRelative;

    std::string scriptText(32768, '\0');
    const int32_t readLength = EditorSession_ScriptRead(
        m_Session, scriptIndex, scriptText.data(),
        static_cast<int32_t>(scriptText.size()));
    ASSERT_GT(readLength, 0) << SessionError(m_Session);
    scriptText.resize(static_cast<size_t>(readLength));
    EXPECT_EQ(scriptText, ReadFile(scratchScript));

    const std::string repoScriptBefore = ReadFile(repoScript);
    scriptText += "\n-- m1a rooted write\n";
    ASSERT_EQ(EditorSession_ScriptSave(m_Session, scriptIndex,
                                       scriptText.c_str()),
              0)
        << SessionError(m_Session);
    EXPECT_EQ(ReadFile(scratchScript), scriptText);
    EXPECT_EQ(ReadFile(repoScript), repoScriptBefore)
        << "ScriptSave must never write through the process CWD";
}

// ── K4.4：旧格式根外绝对路径 → 保留未解析 + 警告；保存 fail-closed ──

TEST_F(ProjectRootBridgeTest, LegacyAbsoluteEntryStaysUnresolvedAndSaveFailsClosed) {
    const fs::path manifestPath = m_RootPath / kManifest;
    nlohmann::json j = nlohmann::json::parse(ReadFile(manifestPath));
    j["assets"].push_back({
        {"guid", "0f1e2d3c4b5a69780000000000000001"},
        {"path", (RepoRoot() / "assets" / "gp01" / "tex" / "pad.png")
                     .generic_string()},
        {"type", "Texture"},
    });
    {
        std::ofstream out(manifestPath, std::ios::binary | std::ios::trunc);
        out << j.dump(2);
    }

    ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              1)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);

    char warn[1024] = {};
    EditorSession_GetWarnings(m_Session, warn, sizeof(warn));
    EXPECT_NE(std::string(warn).find("kept unresolved"), std::string::npos)
        << warn;
    EXPECT_NE(std::string(warn).find("EscapesRoot"), std::string::npos) << warn;

    // 显式保存必须整体失败，且在任何文件写入之前失败（不静默保留旧形式）
    const std::string manifestBefore = ReadFile(manifestPath);
    const fs::path scenePath = m_RootPath / kScene;
    const auto sceneBefore = fs::last_write_time(scenePath);
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 0)
        << SessionError(m_Session);
    EXPECT_EQ(ReadFile(manifestPath), manifestBefore);
    EXPECT_EQ(fs::last_write_time(scenePath), sceneBefore);
}

// ── 非 ASCII 根：K4.2 的 UTF-8 在清单/场景/脚本的真实 IO 上生效 ──
//
// 本机 ACP=936；若任一 OS 边界退回窄字符串（fs::path(std::string) /
// std::ifstream(std::string)），UTF-8 字节会被按 GBK 解释而打不开文件。
// 因此该用例把整个 GP01 副本放进非 ASCII 目录再走生产导出。

TEST(ProjectRootUtf8Test, NonAsciiRootOpensSavesAndScriptsThroughRealFiles) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    const fs::path oldCwd = fs::current_path();
    fs::path scratch;
    fs::path rootPath;
    std::string root;
    std::string error;
    ASSERT_TRUE(PrepareScratch(&scratch, &rootPath, &root, &error)) << error;
    ScratchProjectGuard guard(oldCwd, scratch);

    const fs::path nonAsciiPath =
        rootPath / Engine::Content::PathFromUtf8("工程Ä");
    std::error_code ec;
    fs::create_directories(nonAsciiPath, ec);
    ASSERT_FALSE(ec) << ec.message();
    fs::rename(rootPath / "assets", nonAsciiPath / "assets", ec);
    ASSERT_FALSE(ec) << ec.message();
    const std::string nonAsciiRoot =
        Engine::Content::PathToUtf8(nonAsciiPath);

    EditorSessionHandle session = EditorSession_Create();
    ASSERT_NE(session, nullptr);
    ASSERT_EQ(EditorSession_OpenProjectRooted(session, nonAsciiRoot.c_str(),
                                              kManifest, kScene),
              1)
        << SessionError(session);
    EXPECT_EQ(EditorSession_GetEntityCount(session), 10);

    const fs::path scenePath = nonAsciiPath / kScene;
    const auto sceneBefore = fs::last_write_time(scenePath);
    EXPECT_EQ(EditorSession_CreateEntity(session, "Utf8Probe"), 10);
    ASSERT_EQ(EditorSession_SaveProject(session), 1) << SessionError(session);
    EXPECT_GT(fs::last_write_time(scenePath), sceneBefore);

    int32_t scriptIndex = -1;
    for (int32_t i = 0; i < EditorSession_GetAssetCount(session); ++i) {
        if (EditorSession_GetAssetType(session, i) == 1) {
            scriptIndex = i;
            break;
        }
    }
    ASSERT_GE(scriptIndex, 0);
    char assetPath[512] = {};
    ASSERT_GE(EditorSession_GetAssetPath(session, scriptIndex, assetPath,
                                         sizeof(assetPath)),
              0);
    const fs::path scriptPath =
        nonAsciiPath / Engine::Content::PathFromUtf8(assetPath);

    std::string scriptText(32768, '\0');
    const int32_t readLength = EditorSession_ScriptRead(
        session, scriptIndex, scriptText.data(),
        static_cast<int32_t>(scriptText.size()));
    ASSERT_GT(readLength, 0) << SessionError(session);
    scriptText.resize(static_cast<size_t>(readLength));
    EXPECT_EQ(scriptText, ReadFile(scriptPath));

    scriptText += "\n-- m1a utf8 rooted write\n";
    ASSERT_EQ(EditorSession_ScriptSave(session, scriptIndex,
                                       scriptText.c_str()),
              0)
        << SessionError(session);
    EXPECT_EQ(ReadFile(scriptPath), scriptText);

    EditorSession_Destroy(session);
}

// ── M1-B：加载失败不得破坏已有有效会话状态 ─────────────────────

TEST_F(ProjectRootBridgeTest, StrictLoadFailureLeavesExistingSessionUsable) {
    ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              1)
        << SessionError(m_Session);
    ASSERT_EQ(EditorSession_GetEntityCount(m_Session), 10);
    const int32_t assetsBefore = EditorSession_GetAssetCount(m_Session);
    ASSERT_GT(assetsBefore, 0);

    // 1) 重复 GUID：严格加载失败，旧会话原样保留
    nlohmann::json dup =
        nlohmann::json::parse(ReadFile(m_RootPath / kManifest));
    dup["assets"].push_back(dup["assets"][0]);
    {
        std::ofstream out(m_RootPath / "assets" / "gp01" /
                              "strict_dup.manifest.json",
                          std::ios::binary | std::ios::trunc);
        out << dup.dump(2);
    }
    EXPECT_EQ(EditorSession_OpenProjectRooted(
                  m_Session, m_Root.c_str(),
                  "assets/gp01/strict_dup.manifest.json", kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("manifest load failed"),
              std::string::npos)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), assetsBefore);

    // 2) 损坏清单：同上
    {
        std::ofstream out(m_RootPath / "assets" / "gp01" /
                              "strict_broken.manifest.json",
                          std::ios::binary | std::ios::trunc);
        out << "{ broken";
    }
    EXPECT_EQ(EditorSession_OpenProjectRooted(
                  m_Session, m_Root.c_str(),
                  "assets/gp01/strict_broken.manifest.json", kScene),
              0);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), assetsBefore);

    // 3) 场景损坏：清单解析成功也不得部分提交
    {
        std::ofstream out(m_RootPath / "assets" / "gp01" / "strict_bad.scene",
                          std::ios::binary | std::ios::trunc);
        out << "{ broken";
    }
    EXPECT_EQ(EditorSession_OpenProjectRooted(
                  m_Session, m_Root.c_str(), kManifest,
                  "assets/gp01/strict_bad.scene"),
              0);
    EXPECT_NE(SessionError(m_Session).find("scene load failed"),
              std::string::npos)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), assetsBefore);

    // 4) 旧会话仍可继续编辑与保存
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "AfterFailedOpen"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
}

// ── EngineHost 生产入口：同一 rooted open 契约 ──────────────────
//
// EngineHost 持有 GLFW 持久生命周期，EditorSession 是 legacy 生命周期；
// GlfwLifetime 明确禁止二者在同一进程内并存（AcquireGlfwLifetime 对
// persistent 与 legacy 用户互斥）。本用例因此不能复用会创建 EditorSession
// 的 fixture —— 这是生产约束，不是测试隔离缺陷。

TEST(ProjectRootEngineHostTest, RootedOpenFromForeignCwd) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    const fs::path oldCwd = fs::current_path();
    fs::path scratch;
    fs::path rootPath;
    std::string root;
    std::string error;
    ASSERT_TRUE(PrepareScratch(&scratch, &rootPath, &root, &error)) << error;
    ScratchProjectGuard guard(oldCwd, scratch);

    std::error_code ec;
    fs::current_path(scratch / "empty-cwd", ec);
    ASSERT_FALSE(ec) << ec.message();

    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr) << "EngineHost_Create failed: status="
                             << result.status << " stage=" << result.stage;
    ASSERT_EQ(result.status, ENGINE_HOST_STATUS_SUCCESS);

    EXPECT_EQ(EngineHost_OpenProjectRooted(host, root.c_str(),
                                           "../manifest.json", kScene, &result),
              -1);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SESSION_FAILED);
    EXPECT_EQ(result.stage, ENGINE_HOST_STAGE_OPEN_PROJECT);
    char diagnostic[512] = {};
    ASSERT_GT(EngineHost_GetLastDiagnostic(host, diagnostic,
                                           sizeof(diagnostic)),
              0);
    EXPECT_NE(std::string(diagnostic).find("DotDotSegment"), std::string::npos)
        << diagnostic;

    ASSERT_EQ(EngineHost_OpenProjectRooted(host, root.c_str(), kManifest,
                                           kScene, &result),
              0)
        << diagnostic;
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_SUCCESS);

    ASSERT_EQ(EngineHost_CloseProject(host, &result), 0);
    EngineHost_Destroy(host);
}

// ── M1-E：EngineHost op 11 与无头导入走同一统一服务 ──────────────

TEST(ProjectRootEngineHostTest, SourceImportViaHostOpUsesUnifiedService) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    const fs::path oldCwd = fs::current_path();
    fs::path scratch;
    fs::path rootPath;
    std::string root;
    std::string error;
    ASSERT_TRUE(PrepareScratch(&scratch, &rootPath, &root, &error)) << error;
    ScratchProjectGuard guard(oldCwd, scratch);

    RuntimeResult result = {};
    EngineHostHandle host = EngineHost_Create(&result);
    ASSERT_NE(host, nullptr);
    ASSERT_EQ(result.status, ENGINE_HOST_STATUS_SUCCESS);
    ASSERT_EQ(EngineHost_OpenProjectRooted(host, root.c_str(), kManifest,
                                           kScene, &result),
              0)
;

    const int32_t before = EngineHost_Dispatch(
        host, ENGINE_HOST_OP_GET_ASSET_COUNT, 0, 0, nullptr, nullptr, nullptr,
        nullptr, &result);
    ASSERT_GE(before, 0);

    const std::string source =
        (RepoRoot() / "assets" / "gp01" / "tex" / "pad.png").generic_string();
    const int32_t index = EngineHost_Dispatch(
        host, ENGINE_HOST_OP_IMPORT_ASSET, 0, 0, source.c_str(), nullptr,
        nullptr, nullptr, &result);
    ASSERT_GE(index, 0);
    EXPECT_EQ(EngineHost_Dispatch(host, ENGINE_HOST_OP_GET_ASSET_COUNT, 0, 0,
                                  nullptr, nullptr, nullptr, nullptr,
                                  &result),
              before + 1);
    EXPECT_TRUE(fs::exists(rootPath / "assets" / "imported" / "tex" /
                           "pad.png"));
    EXPECT_TRUE(fs::exists(rootPath / ".engine-content" / "current"));

    ASSERT_EQ(EngineHost_CloseProject(host, &result), 0);
    EngineHost_Destroy(host);
}
