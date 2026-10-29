/**
 * @file test_revision_commit.cpp
 * @brief Content v2 M1-D：current/Revision 单一提交点门禁
 *
 * 覆盖：保存发布 revision + current；重启只读 current；current 损坏/
 * 校验和不符 → RecoveryRequired；孤立高 revision 不晋升；无 current 回退
 * 旧格式；rename 使用已提交 Scene；指针交换失败保留旧 revision 且不复用编号。
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

nlohmann::json ReadJson(const fs::path& path) {
    return nlohmann::json::parse(ReadFile(path));
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

int32_t FindAssetByPath(EditorSessionHandle session, const std::string& path) {
    for (int32_t i = 0; i < EditorSession_GetAssetCount(session); ++i)
        if (AssetPath(session, i) == path) return i;
    return -1;
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

class RevisionCommitTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
        m_OldCwd = fs::current_path();
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_Scratch = fs::temp_directory_path() /
                    ("engine_m1d_revision_" + std::to_string(stamp) + "_" +
                     std::to_string(g_Unique.fetch_add(1)));
        std::error_code ec;
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

    void Reopen() {
        EditorSession_Destroy(m_Session);
        m_Session = EditorSession_Create();
        ASSERT_NE(m_Session, nullptr);
        ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                                  kManifest, kScene),
                  1)
            << SessionError(m_Session);
    }

    fs::path CurrentPath() const {
        return m_RootPath / ".engine-content" / "current";
    }

    fs::path RevisionPath(int64_t revision) const {
        return m_RootPath / ".engine-content" / "revisions" /
               std::to_string(revision);
    }

    fs::path m_OldCwd;
    fs::path m_Scratch;
    fs::path m_RootPath;
    std::string m_Root;
    EditorSessionHandle m_Session = nullptr;
};

TEST_F(RevisionCommitTest, SavePublishesCurrentAndRestartUsesRevision) {
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "RevProbe"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);

    ASSERT_TRUE(fs::exists(CurrentPath()));
    const auto current = ReadJson(CurrentPath());
    EXPECT_EQ(current["revision"].get<int64_t>(), 1);
    EXPECT_EQ(current["manifest"].get<std::string>(),
              "revisions/1/manifest.json");
    EXPECT_EQ(current["scene"].get<std::string>(), "revisions/1/scene.scene");
    EXPECT_TRUE(fs::exists(RevisionPath(1) / "manifest.json"));
    EXPECT_TRUE(fs::exists(RevisionPath(1) / "scene.scene"));
    EXPECT_FALSE(current["manifestHash"].get<std::string>().empty());
    EXPECT_FALSE(current["sceneHash"].get<std::string>().empty());

    // 旧路径只是镜像；破坏它们后，重启仍必须从 current 的 revision 加载。
    WriteFile(m_RootPath / kManifest, "{ broken legacy manifest");
    WriteFile(m_RootPath / kScene, "{ broken legacy scene");
    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 11);
}

TEST_F(RevisionCommitTest, CorruptCurrentIsRecoveryRequired) {
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    WriteFile(CurrentPath(), "{ not valid json");

    EditorSession_Destroy(m_Session);
    m_Session = EditorSession_Create();
    ASSERT_NE(m_Session, nullptr);
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), -1);
}

TEST_F(RevisionCommitTest, ChecksumMismatchIsRecoveryRequired) {
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    const fs::path manifest = RevisionPath(1) / "manifest.json";
    WriteFile(manifest, ReadFile(manifest) + " ");

    EditorSession_Destroy(m_Session);
    m_Session = EditorSession_Create();
    ASSERT_NE(m_Session, nullptr);
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos)
        << SessionError(m_Session);
}

TEST_F(RevisionCommitTest, OrphanHigherRevisionIsNotPromoted) {
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "CurrentRev"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);

    // 更高 revision 的孤立候选（无 current 指向）不得被读取或升级。
    std::error_code ec;
    fs::create_directories(RevisionPath(99), ec);
    ASSERT_FALSE(ec) << ec.message();
    WriteFile(RevisionPath(99) / "manifest.json",
              R"({"version":1,"assets":[]})");
    WriteFile(RevisionPath(99) / "scene.scene",
              R"({"version":1,"entities":[]})");

    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 11);
    EXPECT_EQ(ReadJson(CurrentPath())["revision"].get<int64_t>(), 1);
}

TEST_F(RevisionCommitTest, MissingCurrentIgnoresRevisionDirsAndStartsFresh) {
    std::error_code ec;
    fs::create_directories(RevisionPath(5), ec);
    ASSERT_FALSE(ec) << ec.message();
    WriteFile(RevisionPath(5) / "manifest.json",
              R"({"version":1,"assets":[]})");
    WriteFile(RevisionPath(5) / "scene.scene",
              R"({"version":1,"entities":[]})");

    // 无 current：按旧格式从 legacy 文件打开（不得扫描 revisions/5）。
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);

    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "AfterOrphan"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    // 无 current 时没有基线 revision；从 1 开始，孤立目录保持原样。
    EXPECT_EQ(ReadJson(CurrentPath())["revision"].get<int64_t>(), 1);
    EXPECT_EQ(ReadJson(RevisionPath(5) / "scene.scene")["entities"].size(), 0u);
    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 11);
}

TEST_F(RevisionCommitTest, RenamePublishesCommittedSceneNotLiveEdits) {
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "Unsaved"), 10);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 1);

    const int32_t index =
        FindAssetByPath(m_Session, "assets/gp01/tex/pad.png");
    ASSERT_GE(index, 0);
    ASSERT_EQ(EditorSession_RenameAsset(m_Session, index, "pad_rev"), 0)
        << SessionError(m_Session);

    // rename 的 revision 使用已提交 Scene（10 实体），不吞掉未保存编辑。
    ASSERT_TRUE(fs::exists(CurrentPath()));
    EXPECT_EQ(ReadJson(CurrentPath())["revision"].get<int64_t>(), 1);
    const auto committedScene =
        ReadJson(RevisionPath(1) / "scene.scene");
    EXPECT_EQ(committedScene["entities"].size(), 10u);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 11);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 1);

    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10)
        << "restart must see the committed scene, not unsaved edits";
    EXPECT_GE(FindAssetByPath(m_Session, "assets/gp01/tex/pad_rev.png"), 0);
}

#if defined(_WIN32)
TEST_F(RevisionCommitTest, FailedCurrentSwapKeepsOldRevisionAndSkipsNumber) {
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "Rev1"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    const std::string currentBefore = ReadFile(CurrentPath());

    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "Rev2"), 11);

    // 真实占用：句柄不含 FILE_SHARE_DELETE → current 的原子替换失败。
    struct ScopedHandle {
        HANDLE h = INVALID_HANDLE_VALUE;
        ~ScopedHandle() {
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
    } guard;
    guard.h = CreateFileW(CurrentPath().wstring().c_str(), GENERIC_READ,
                          0, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(guard.h, INVALID_HANDLE_VALUE);

    EXPECT_EQ(EditorSession_SaveProject(m_Session), 0)
        << "pointer swap must fail while current is exclusively held";
    EXPECT_NE(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos)
        << "unreadable current after failed replace must be RecoveryRequired: "
        << SessionError(m_Session);
    LARGE_INTEGER lockedSize = {};
    ASSERT_TRUE(GetFileSizeEx(guard.h, &lockedSize));
    EXPECT_EQ(lockedSize.QuadPart,
              static_cast<LONGLONG>(currentBefore.size()))
        << "failed pointer swap must keep the old current bytes";
    CloseHandle(guard.h);
    guard.h = INVALID_HANDLE_VALUE;
    EXPECT_EQ(ReadFile(CurrentPath()), currentBefore);

    // 失败后重启：只能看到旧完整 revision（11 实体，不含未提交的 Rev2）。
    // （同一会话因 RecoveryRequired 已禁止继续保存，必须重新打开。）
    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 11);
    EXPECT_EQ(ReadJson(CurrentPath())["revision"].get<int64_t>(), 1);

    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "Rev2Again"), 11);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    EXPECT_EQ(ReadJson(CurrentPath())["revision"].get<int64_t>(), 2);

    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 12);
}

TEST_F(RevisionCommitTest, LegacyMirrorFailureIsCommittedCleanupPending) {
    ASSERT_TRUE(SetReadOnly(m_RootPath / kManifest, true));
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "MirrorFail"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    EXPECT_TRUE(fs::exists(CurrentPath()));
    EXPECT_NE(SessionWarnings(m_Session).find("CommittedCleanupPending"),
              std::string::npos)
        << SessionWarnings(m_Session);

    // current 已提交：重启仍加载新 revision。
    EditorSession_Destroy(m_Session);
    m_Session = EditorSession_Create();
    ASSERT_NE(m_Session, nullptr);
    ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              1)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 11);
    ASSERT_TRUE(SetReadOnly(m_RootPath / kManifest, false));
}
#endif // _WIN32

TEST_F(RevisionCommitTest, CandidateWriteFailureKeepsOldCurrent) {
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "Rev1"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    const std::string currentBefore = ReadFile(CurrentPath());

    // 让候选 revision 目录不可创建：revisions 变成普通文件。
    const fs::path revisionsDir = m_RootPath / ".engine-content" / "revisions";
    std::error_code ec;
    fs::remove_all(revisionsDir, ec);
    ASSERT_FALSE(ec) << ec.message();
    WriteFile(revisionsDir, "block");

    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "Rev2"), 11);
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 0)
        << "candidate revision write must fail";
    EXPECT_NE(SessionError(m_Session).find("NotCommitted"), std::string::npos)
        << SessionError(m_Session);
    EXPECT_EQ(ReadFile(CurrentPath()), currentBefore)
        << "old current stays authoritative";

    // 恢复目录后重试：候选未占用 revision 编号。
    fs::remove(revisionsDir, ec);
    ASSERT_FALSE(ec) << ec.message();
    fs::create_directories(revisionsDir, ec);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    EXPECT_EQ(ReadJson(CurrentPath())["revision"].get<int64_t>(), 2);
    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 12);
}

TEST_F(RevisionCommitTest, CurrentLayoutMismatchIsRecoveryRequired) {
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    auto current = ReadJson(CurrentPath());
    current["manifest"] = "assets/gp01/manifest.json";
    WriteFile(CurrentPath(), current.dump(2));

    EditorSession_Destroy(m_Session);
    m_Session = EditorSession_Create();
    ASSERT_NE(m_Session, nullptr);
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos)
        << SessionError(m_Session);
}

TEST_F(RevisionCommitTest, MissingRevisionFileIsRecoveryRequired) {
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    std::error_code ec;
    fs::remove(RevisionPath(1) / "scene.scene", ec);
    ASSERT_FALSE(ec) << ec.message();

    EditorSession_Destroy(m_Session);
    m_Session = EditorSession_Create();
    ASSERT_NE(m_Session, nullptr);
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos)
        << SessionError(m_Session);
}

TEST_F(RevisionCommitTest, LegacyProjectWithoutMetadataIsUnversioned) {
    EXPECT_FALSE(fs::exists(m_RootPath / ".engine-content"))
        << "legacy open must not create commit metadata";
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
}

TEST_F(RevisionCommitTest, VersionedProjectMissingCurrentIsRecoveryRequired) {
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    ASSERT_TRUE(fs::exists(m_RootPath / ".engine-content" / "versioned"));
    std::error_code ec;
    fs::remove(CurrentPath(), ec);
    ASSERT_FALSE(ec) << ec.message();

    EditorSession_Destroy(m_Session);
    m_Session = EditorSession_Create();
    ASSERT_NE(m_Session, nullptr);
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos)
        << SessionError(m_Session);
}

TEST_F(RevisionCommitTest, StaleSessionSaveIsRejected) {
    EditorSessionHandle second = EditorSession_Create();
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(EditorSession_OpenProjectRooted(second, m_Root.c_str(),
                                              kManifest, kScene),
              1);

    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "WriterA"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);

    EXPECT_EQ(EditorSession_CreateEntity(second, "WriterB"), 10);
    EXPECT_EQ(EditorSession_SaveProject(second), 0)
        << "stale writer must not overwrite a newer current";
    EXPECT_NE(SessionError(second).find("stale baseline"),
              std::string::npos)
        << SessionError(second);
    EXPECT_EQ(ReadJson(CurrentPath())["revision"].get<int64_t>(), 1);

    EditorSession_Destroy(second);
    second = EditorSession_Create();
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(EditorSession_OpenProjectRooted(second, m_Root.c_str(),
                                              kManifest, kScene),
              1)
        << SessionError(second);
    char name[128] = {};
    ASSERT_GE(EditorSession_GetEntityName(second, 10, name, sizeof(name)), 0);
    EXPECT_STREQ(name, "WriterA") << "newer committed state must survive";
    EditorSession_Destroy(second);
}

TEST_F(RevisionCommitTest, FirstPublishFailureKeepsLegacyCompatibility) {
    // 全新旧格式工程：首次发布失败不得写 versioned 标记，也不得污染旧格式打开。
    const fs::path revisionsDir = m_RootPath / ".engine-content" / "revisions";
    std::error_code ec;
    fs::create_directories(m_RootPath / ".engine-content", ec);
    ASSERT_FALSE(ec) << ec.message();
    WriteFile(revisionsDir, "block");

    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "FirstTry"), 10);
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 0)
        << "first publish must fail";
    EXPECT_NE(SessionError(m_Session).find("NotCommitted"), std::string::npos)
        << SessionError(m_Session);
    EXPECT_FALSE(fs::exists(CurrentPath()));
    EXPECT_FALSE(fs::exists(m_RootPath / ".engine-content" / "versioned"));

    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10)
        << "failed first publish must leave a valid legacy project";

    fs::remove(revisionsDir, ec);
    ASSERT_FALSE(ec) << ec.message();
    fs::create_directories(revisionsDir, ec);
    ASSERT_FALSE(ec) << ec.message();
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "FirstCommitted"), 10);
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    EXPECT_EQ(ReadJson(CurrentPath())["revision"].get<int64_t>(), 1);
    EXPECT_TRUE(fs::exists(m_RootPath / ".engine-content" / "versioned"));
    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 11);
}
