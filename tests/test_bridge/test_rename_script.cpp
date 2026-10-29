/**
 * @file test_rename_script.cpp
 * @brief Content v2 M1-C：Rename 两阶段与 ScriptSave 可靠写入门禁
 *
 * 覆盖：copy → manifest 提交 → 删除旧源；目标占用/注册冲突；no-op；
 * 大小写-only rename；清理失败 CommittedCleanupPending；提交失败零半更新；
 * ScriptSave 原子替换且失败保留原文件。
 */

#include <gtest/gtest.h>
#include "editor_bridge/capi.h"

#include <Engine/Core/Content/Utf8Path.h>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kManifest = "assets/gp01/manifest.json";
constexpr const char* kScene = "assets/gp01/Main.scene";

std::atomic<int> g_Unique{0};

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

void WriteFile(const fs::path& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << body;
}

std::string SessionError(EditorSessionHandle session) {
    char buf[1024] = {};
    EditorSession_GetLastError(session, buf, sizeof(buf));
    return buf;
}

std::string SessionWarnings(EditorSessionHandle session) {
    char buf[2048] = {};
    EditorSession_GetWarnings(session, buf, sizeof(buf));
    return buf;
}

std::string AssetPath(EditorSessionHandle session, int32_t index) {
    char buf[512] = {};
    if (EditorSession_GetAssetPath(session, index, buf, sizeof(buf)) < 0)
        return {};
    return buf;
}

std::string AssetGuid(EditorSessionHandle session, int32_t index) {
    char buf[64] = {};
    if (EditorSession_GetAssetGuid(session, index, buf, sizeof(buf)) < 0)
        return {};
    return buf;
}

int32_t FindAssetByPath(EditorSessionHandle session, const std::string& path) {
    const int32_t n = EditorSession_GetAssetCount(session);
    for (int32_t i = 0; i < n; ++i)
        if (AssetPath(session, i) == path) return i;
    return -1;
}

nlohmann::json ReadManifest(const fs::path& root) {
    return nlohmann::json::parse(ReadFile(root / kManifest));
}

std::string ManifestPathForGuid(const nlohmann::json& manifest,
                                const std::string& guidHex) {
    for (const auto& a : manifest["assets"]) {
        if (a["guid"].get<std::string>() == guidHex)
            return a["path"].get<std::string>();
    }
    return {};
}

bool DirectoryHasExactName(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (Engine::Content::PathToUtf8(it->path().filename()) == name)
            return true;
    }
    return false;
}

bool DirectoryHasTempArtifacts(const fs::path& dir) {
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string name =
            Engine::Content::PathToUtf8(it->path().filename());
        if (name.rfind(".m1tmp.", 0) == 0 ||
            name.rfind(".m1c_case_", 0) == 0)
            return true;
    }
    return false;
}

#if defined(_WIN32)
bool SetReadOnly(const fs::path& path, bool readOnly) {
    const std::wstring wide = path.wstring();
    DWORD attrs = GetFileAttributesW(wide.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (readOnly)
        attrs |= FILE_ATTRIBUTE_READONLY;
    else
        attrs &= ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY);
    return SetFileAttributesW(wide.c_str(), attrs) != 0;
}
#endif

} // namespace

class RenameScriptTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
        m_OldCwd = fs::current_path();

        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_Scratch = fs::temp_directory_path() /
                    ("engine_m1c_rename_" + std::to_string(stamp) + "_" +
                     std::to_string(g_Unique.fetch_add(1)));
        std::error_code ec;
        fs::create_directories(m_Scratch / "empty-cwd", ec);
        ASSERT_FALSE(ec) << ec.message();
        fs::create_directories(m_Scratch / "assets", ec);
        ASSERT_FALSE(ec) << ec.message();
        fs::copy(RepoRoot() / "assets" / "gp01", m_Scratch / "assets" / "gp01",
                 fs::copy_options::recursive |
                     fs::copy_options::overwrite_existing,
                 ec);
        ASSERT_FALSE(ec) << ec.message();
        m_RootPath = fs::canonical(m_Scratch, ec);
        ASSERT_FALSE(ec) << ec.message();
        m_Root = Engine::Content::PathToUtf8(m_RootPath);

        m_Session = EditorSession_Create();
        ASSERT_NE(m_Session, nullptr);
        ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                                  kManifest, kScene),
                  1)
            << SessionError(m_Session);
    }

    void TearDown() override {
        if (m_Session) EditorSession_Destroy(m_Session);
        std::error_code ec;
        fs::current_path(m_OldCwd, ec);
#if defined(_WIN32)
        for (auto it = fs::recursive_directory_iterator(m_Scratch, ec);
             !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it->is_regular_file(ec)) SetReadOnly(it->path(), false);
        }
#endif
        fs::remove_all(m_Scratch, ec);
    }

    fs::path m_OldCwd;
    fs::path m_Scratch;
    fs::path m_RootPath;
    std::string m_Root;
    EditorSessionHandle m_Session = nullptr;
};

TEST_F(RenameScriptTest, RenameCopiesCommitsAndPreservesIdentity) {
    const std::string oldRel = "assets/gp01/tex/pad.png";
    const int32_t index = FindAssetByPath(m_Session, oldRel);
    ASSERT_GE(index, 0);
    const std::string guidBefore = AssetGuid(m_Session, index);
    ASSERT_FALSE(guidBefore.empty());

    const fs::path oldAbs = m_RootPath / oldRel;
    const fs::path newAbs = m_RootPath / "assets/gp01/tex/pad_m1c.png";
    const std::string oldBytes = ReadFile(oldAbs);
    const std::string sceneBefore = ReadFile(m_RootPath / kScene);

    ASSERT_EQ(EditorSession_RenameAsset(m_Session, index, "pad_m1c"), 0)
        << SessionError(m_Session);

    // 物理：old 删除、new 字节一致（copy → commit → delete）
    EXPECT_FALSE(fs::exists(oldAbs));
    ASSERT_TRUE(fs::exists(newAbs));
    EXPECT_EQ(ReadFile(newAbs), oldBytes);
    EXPECT_EQ(ReadFile(m_RootPath / kScene), sceneBefore)
        << "rename must not touch the scene (GUID references stay valid)";

    // 注册表：GUID 不变、路径更新
    EXPECT_EQ(AssetGuid(m_Session, index), guidBefore);
    EXPECT_EQ(AssetPath(m_Session, index),
              "assets/gp01/tex/pad_m1c.png");

    // Rename 自身即提交 manifest（不需要再 SaveProject）
    auto manifest = ReadManifest(m_RootPath);
    EXPECT_EQ(ManifestPathForGuid(manifest, guidBefore),
              "assets/gp01/tex/pad_m1c.png");
    EXPECT_EQ(manifest.dump().find("pad.png"), std::string::npos);

    // 重新打开同一工程：新路径仍是同一 GUID
    EditorSession_Destroy(m_Session);
    m_Session = EditorSession_Create();
    ASSERT_NE(m_Session, nullptr);
    ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              1)
        << SessionError(m_Session);
    const int32_t reopened =
        FindAssetByPath(m_Session, "assets/gp01/tex/pad_m1c.png");
    ASSERT_GE(reopened, 0);
    EXPECT_EQ(AssetGuid(m_Session, reopened), guidBefore);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
}

TEST_F(RenameScriptTest, RenameRejectsExistingTargetWithoutOverwrite) {
    const std::string oldRel = "assets/gp01/tex/pad.png";
    const int32_t index = FindAssetByPath(m_Session, oldRel);
    ASSERT_GE(index, 0);
    const std::string guidBefore = AssetGuid(m_Session, index);
    const std::string manifestBefore = ReadFile(m_RootPath / kManifest);
    const std::string oldBytes =
        ReadFile(m_RootPath / "assets/gp01/tex/pad.png");

    const fs::path taken = m_RootPath / "assets/gp01/tex/pad_taken.png";
    WriteFile(taken, "sentinel-unregistered");
    const std::string sentinel = ReadFile(taken);

    EXPECT_NE(EditorSession_RenameAsset(m_Session, index, "pad_taken"), 0);
    EXPECT_EQ(ReadFile(taken), sentinel) << "conflict must not overwrite";
    EXPECT_TRUE(fs::exists(m_RootPath / oldRel));
    EXPECT_EQ(AssetPath(m_Session, index), oldRel);
    EXPECT_EQ(AssetGuid(m_Session, index), guidBefore);
    EXPECT_EQ(ReadFile(m_RootPath / kManifest), manifestBefore);
    EXPECT_FALSE(DirectoryHasTempArtifacts(m_RootPath / "assets/gp01/tex"));
}

TEST_F(RenameScriptTest, RenameRejectsRegisteredPathConflict) {
    const int32_t index =
        FindAssetByPath(m_Session, "assets/gp01/tex/pad.png");
    ASSERT_GE(index, 0);
    const std::string guidBefore = AssetGuid(m_Session, index);
    const std::string manifestBefore = ReadFile(m_RootPath / kManifest);

    // player.png 已被另一 GUID 注册 → PathKey 冲突，必须在任何磁盘动作前拒绝
    EXPECT_NE(EditorSession_RenameAsset(m_Session, index, "player"), 0);
    EXPECT_TRUE(fs::exists(m_RootPath / "assets/gp01/tex/pad.png"));
    EXPECT_TRUE(fs::exists(m_RootPath / "assets/gp01/tex/player.png"));
    EXPECT_EQ(AssetPath(m_Session, index), "assets/gp01/tex/pad.png");
    EXPECT_EQ(AssetGuid(m_Session, index), guidBefore);
    EXPECT_EQ(ReadFile(m_RootPath / kManifest), manifestBefore);
}

TEST_F(RenameScriptTest, RenameToSamePathIsNoOpWithoutCommit) {
    const int32_t index =
        FindAssetByPath(m_Session, "assets/gp01/tex/pad.png");
    ASSERT_GE(index, 0);
    const std::string manifestBefore = ReadFile(m_RootPath / kManifest);
    const auto mtimeBefore =
        fs::last_write_time(m_RootPath / "assets/gp01/tex/pad.png");

    ASSERT_EQ(EditorSession_RenameAsset(m_Session, index, "pad"), 0)
        << SessionError(m_Session);
    EXPECT_EQ(ReadFile(m_RootPath / kManifest), manifestBefore);
    EXPECT_EQ(fs::last_write_time(m_RootPath / "assets/gp01/tex/pad.png"),
              mtimeBefore);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 0)
        << "no-op rename must not dirty the session";
}

TEST_F(RenameScriptTest, CaseOnlyRenameUpdatesCaseAndKeepsGuid) {
    const std::string oldRel = "assets/gp01/tex/pad.png";
    const int32_t index = FindAssetByPath(m_Session, oldRel);
    ASSERT_GE(index, 0);
    const std::string guidBefore = AssetGuid(m_Session, index);

    ASSERT_EQ(EditorSession_RenameAsset(m_Session, index, "PAD"), 0)
        << SessionError(m_Session);

    const fs::path texDir = m_RootPath / "assets/gp01/tex";
    EXPECT_TRUE(DirectoryHasExactName(texDir, "PAD.png"));
    EXPECT_EQ(AssetPath(m_Session, index), "assets/gp01/tex/PAD.png");
    EXPECT_EQ(AssetGuid(m_Session, index), guidBefore);
    EXPECT_EQ(ManifestPathForGuid(ReadManifest(m_RootPath), guidBefore),
              "assets/gp01/tex/PAD.png");
    EXPECT_FALSE(DirectoryHasTempArtifacts(texDir));
}

#if defined(_WIN32)
TEST_F(RenameScriptTest, CleanupFailureIsCommittedCleanupPending) {
    const std::string oldRel = "assets/gp01/tex/pad.png";
    const int32_t index = FindAssetByPath(m_Session, oldRel);
    ASSERT_GE(index, 0);
    const std::string guidBefore = AssetGuid(m_Session, index);
    const fs::path oldAbs = m_RootPath / oldRel;
    const fs::path newAbs = m_RootPath / "assets/gp01/tex/pad_ro.png";

    // 真实占用：句柄不含 FILE_SHARE_DELETE → 提交后的清理删除会失败。
    struct ScopedHandle {
        HANDLE h = INVALID_HANDLE_VALUE;
        ~ScopedHandle() {
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
    } guard;
    guard.h = CreateFileW(oldAbs.wstring().c_str(), GENERIC_READ,
                          FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(guard.h, INVALID_HANDLE_VALUE);

    ASSERT_EQ(EditorSession_RenameAsset(m_Session, index, "pad_ro"), 0)
        << SessionError(m_Session);
    EXPECT_TRUE(fs::exists(newAbs));
    EXPECT_TRUE(fs::exists(oldAbs)) << "cleanup failure keeps old source";
    EXPECT_EQ(AssetPath(m_Session, index),
              "assets/gp01/tex/pad_ro.png");
    EXPECT_EQ(AssetGuid(m_Session, index), guidBefore);
    EXPECT_NE(SessionWarnings(m_Session).find("CommittedCleanupPending"),
              std::string::npos)
        << SessionWarnings(m_Session);

    // 清理可重试：释放句柄后，下一次保存前重试成功
    CloseHandle(guard.h);
    guard.h = INVALID_HANDLE_VALUE;
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    EXPECT_FALSE(fs::exists(oldAbs));
}

TEST_F(RenameScriptTest, CommitFailureLeavesOldSourceAndNoHalfUpdate) {
    // 先提交一次，生成 current；随后让 current 无法替换（指针交换失败）。
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    const fs::path currentAbs = m_RootPath / ".engine-content" / "current";
    ASSERT_TRUE(fs::exists(currentAbs));
    const std::string currentBefore = ReadFile(currentAbs);
    const std::string manifestBefore = ReadFile(m_RootPath / kManifest);

    const std::string oldRel = "assets/gp01/tex/pad.png";
    const int32_t index = FindAssetByPath(m_Session, oldRel);
    ASSERT_GE(index, 0);
    const std::string guidBefore = AssetGuid(m_Session, index);

    ASSERT_TRUE(SetReadOnly(currentAbs, true));

    EXPECT_NE(EditorSession_RenameAsset(m_Session, index, "pad_blocked"), 0);
    EXPECT_TRUE(fs::exists(m_RootPath / oldRel));
    EXPECT_FALSE(fs::exists(m_RootPath / "assets/gp01/tex/pad_blocked.png"))
        << "prepared copy must be removed on commit failure";
    EXPECT_EQ(AssetPath(m_Session, index), oldRel);
    EXPECT_EQ(AssetGuid(m_Session, index), guidBefore);
    EXPECT_EQ(ReadFile(currentAbs), currentBefore)
        << "current must remain the old committed revision";
    EXPECT_EQ(ReadFile(m_RootPath / kManifest), manifestBefore);
    EXPECT_FALSE(DirectoryHasTempArtifacts(m_RootPath / "assets/gp01/tex"));

    ASSERT_TRUE(SetReadOnly(currentAbs, false));
}

TEST_F(RenameScriptTest, ScriptSaveFailureKeepsOriginalFile) {
    int32_t scriptIndex = -1;
    for (int32_t i = 0; i < EditorSession_GetAssetCount(m_Session); ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 1) {
            scriptIndex = i;
            break;
        }
    ASSERT_GE(scriptIndex, 0);
    const std::string rel = AssetPath(m_Session, scriptIndex);
    ASSERT_FALSE(rel.empty());
    const fs::path abs = m_RootPath / rel;
    const std::string original = ReadFile(abs);

    ASSERT_TRUE(SetReadOnly(abs, true));
    EXPECT_NE(EditorSession_ScriptSave(m_Session, scriptIndex,
                                       "-- m1c should not land\n"),
              0);
    EXPECT_EQ(ReadFile(abs), original) << "failure must keep original bytes";

    std::string readBack(32768, '\0');
    const int32_t length = EditorSession_ScriptRead(
        m_Session, scriptIndex, readBack.data(),
        static_cast<int32_t>(readBack.size()));
    ASSERT_GT(length, 0);
    readBack.resize(static_cast<size_t>(length));
    EXPECT_EQ(readBack, original);

    ASSERT_TRUE(SetReadOnly(abs, false));
    EXPECT_FALSE(DirectoryHasTempArtifacts(abs.parent_path()));
}
#endif // _WIN32

TEST_F(RenameScriptTest, ScriptSaveReplacesAtomically) {
    int32_t scriptIndex = -1;
    for (int32_t i = 0; i < EditorSession_GetAssetCount(m_Session); ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 1) {
            scriptIndex = i;
            break;
        }
    ASSERT_GE(scriptIndex, 0);
    const std::string rel = AssetPath(m_Session, scriptIndex);
    const fs::path abs = m_RootPath / rel;
    const std::string guidBefore = AssetGuid(m_Session, scriptIndex);

    const std::string updated = ReadFile(abs) + "\n-- m1c atomic save\n";
    ASSERT_EQ(EditorSession_ScriptSave(m_Session, scriptIndex,
                                       updated.c_str()),
              0)
        << SessionError(m_Session);
    EXPECT_EQ(ReadFile(abs), updated);
    EXPECT_EQ(AssetGuid(m_Session, scriptIndex), guidBefore);
    EXPECT_FALSE(DirectoryHasTempArtifacts(abs.parent_path()));
}
