/**
 * @file test_import_service.cpp
 * @brief Content v2 M1-E：统一导入服务、GUID 身份与显式 reattach 门禁
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
constexpr const char* kPlayerTextureGuid = "d1000000000000000000000000000001";

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
    for (int32_t i = 0; i < EditorSession_GetAssetCount(session); ++i)
        if (AssetPath(session, i) == path) return i;
    return -1;
}

int32_t FindAssetByGuid(EditorSessionHandle session, const std::string& guid) {
    for (int32_t i = 0; i < EditorSession_GetAssetCount(session); ++i)
        if (AssetGuid(session, i) == guid) return i;
    return -1;
}

} // namespace

class ImportServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
        m_OldCwd = fs::current_path();
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        const std::string unique = std::to_string(stamp) + "_" +
                                   std::to_string(g_Unique.fetch_add(1));
        m_Scratch = fs::temp_directory_path() /
                    ("engine_m1e_import_" + unique);
        m_SourceDir = fs::temp_directory_path() /
                      ("engine_m1e_source_" + unique);
        std::error_code ec;
        fs::create_directories(m_Scratch / "assets", ec);
        ASSERT_FALSE(ec) << ec.message();
        fs::create_directories(m_SourceDir, ec);
        ASSERT_FALSE(ec) << ec.message();
        fs::copy(RepoRoot() / "assets" / "gp01", m_Scratch / "assets" / "gp01",
                 fs::copy_options::recursive |
                     fs::copy_options::overwrite_existing,
                 ec);
        ASSERT_FALSE(ec) << ec.message();
        m_RootPath = fs::canonical(m_Scratch, ec);
        ASSERT_FALSE(ec) << ec.message();
        m_Root = Engine::Content::PathToUtf8(m_RootPath);
    }

    void TearDown() override {
        if (m_Session) EditorSession_Destroy(m_Session);
        std::error_code ec;
        fs::current_path(m_OldCwd, ec);
        fs::remove_all(m_Scratch, ec);
        fs::remove_all(m_SourceDir, ec);
    }

    void Open() {
        m_Session = EditorSession_Create();
        ASSERT_NE(m_Session, nullptr);
        ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                                  kManifest, kScene),
                  1)
            << SessionError(m_Session);
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

    void RemoveManifestEntry(const std::string& guid) {
        const fs::path manifest = m_RootPath / kManifest;
        nlohmann::json j = nlohmann::json::parse(ReadFile(manifest));
        auto& assets = j["assets"];
        for (auto it = assets.begin(); it != assets.end(); ++it) {
            if ((*it)["guid"].get<std::string>() == guid) {
                assets.erase(it);
                break;
            }
        }
        WriteFile(manifest, j.dump(2));
    }

    std::string SourceFile(const std::string& name, const std::string& bytes) {
        const fs::path path = m_SourceDir / name;
        WriteFile(path, bytes);
        return Engine::Content::PathToUtf8(path);
    }

    fs::path CurrentPath() const {
        return m_RootPath / ".engine-content" / "current";
    }

    fs::path m_OldCwd;
    fs::path m_Scratch;
    fs::path m_SourceDir;
    fs::path m_RootPath;
    std::string m_Root;
    EditorSessionHandle m_Session = nullptr;
};

TEST_F(ImportServiceTest, SourceImportCopiesCommitsAndSurvivesRestart) {
    Open();
    const int32_t before = EditorSession_GetAssetCount(m_Session);
    const std::string bytes =
        ReadFile(RepoRoot() / "assets/gp01/tex/pad.png");
    const std::string source = SourceFile("pad_copy.png", bytes);

    const int32_t index =
        EditorSession_ImportAsset(m_Session, source.c_str(), 0);
    ASSERT_GE(index, 0) << SessionError(m_Session);
    const std::string path = AssetPath(m_Session, index);
    EXPECT_EQ(path, "assets/imported/tex/pad_copy.png");
    const std::string guid = AssetGuid(m_Session, index);
    ASSERT_FALSE(guid.empty());
    EXPECT_TRUE(fs::exists(m_RootPath / path));
    EXPECT_EQ(ReadFile(m_RootPath / path), bytes);
    EXPECT_EQ(ReadFile(fs::path(source)), bytes) << "source must not be moved";
    EXPECT_TRUE(fs::exists(CurrentPath()));

    Reopen();
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), before + 1);
    const int32_t reopened = FindAssetByPath(m_Session, path);
    ASSERT_GE(reopened, 0);
    EXPECT_EQ(AssetGuid(m_Session, reopened), guid);
}

TEST_F(ImportServiceTest, SameContentDifferentFilesGetDistinctGuids) {
    Open();
    const std::string bytes =
        ReadFile(RepoRoot() / "assets/gp01/tex/pad.png");
    const std::string a = SourceFile("same_a.png", bytes);
    const std::string b = SourceFile("same_b.png", bytes);

    const int32_t ia = EditorSession_ImportAsset(m_Session, a.c_str(), 0);
    const int32_t ib = EditorSession_ImportAsset(m_Session, b.c_str(), 0);
    ASSERT_GE(ia, 0) << SessionError(m_Session);
    ASSERT_GE(ib, 0) << SessionError(m_Session);
    EXPECT_NE(AssetGuid(m_Session, ia), AssetGuid(m_Session, ib))
        << "same content must not reuse GUID";
    EXPECT_NE(AssetPath(m_Session, ia), AssetPath(m_Session, ib));
}

TEST_F(ImportServiceTest, TargetConflictIsRejectedWithoutOverwrite) {
    Open();
    const std::string firstBytes = "first-content";
    const std::string first = SourceFile("dup.png", firstBytes);
    const int32_t index =
        EditorSession_ImportAsset(m_Session, first.c_str(), 0);
    ASSERT_GE(index, 0) << SessionError(m_Session);
    const int32_t countBefore = EditorSession_GetAssetCount(m_Session);
    const std::string currentBefore = ReadFile(CurrentPath());
    const std::string target = AssetPath(m_Session, index);
    const std::string storedBefore = ReadFile(m_RootPath / target);

    const std::string second =
        SourceFile("dup_other.png", "second-content");
    // 把第二份源改名为同名，制造目标 PathKey 冲突
    const fs::path secondPath = fs::path(second);
    const fs::path conflict = secondPath.parent_path() / "dup.png";
    std::error_code ec;
    fs::remove(conflict, ec);
    fs::create_directories(secondPath.parent_path() / "other", ec);
    const std::string conflictSource =
        Engine::Content::PathToUtf8(secondPath.parent_path() / "other" / "dup.png");
    WriteFile(fs::path(conflictSource), "second-content");

    EXPECT_EQ(EditorSession_ImportAsset(m_Session, conflictSource.c_str(), 0),
              -1);
    EXPECT_NE(SessionError(m_Session).find("conflict"), std::string::npos)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), countBefore);
    EXPECT_EQ(ReadFile(m_RootPath / target), storedBefore)
        << "existing target must not be overwritten";
    EXPECT_EQ(ReadFile(CurrentPath()), currentBefore);
}

TEST_F(ImportServiceTest, IllegalTargetPathsAreRejected) {
    Open();
    const int32_t before = EditorSession_GetAssetCount(m_Session);
    const std::string currentBefore = ReadFile(CurrentPath());

    const char* illegal[] = {"../evil.png", "//server/share/a.png",
                             "\\\\?\\C:\\a.png", "C:evil.png"};
    for (const char* path : illegal) {
        EXPECT_EQ(EditorSession_ImportAsset(m_Session, path, 0), -1)
            << "path=" << path;
    }
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), before);
    EXPECT_EQ(ReadFile(CurrentPath()), currentBefore);
}

TEST_F(ImportServiceTest, MissingSourceFailsWithoutCommit) {
    Open();
    const int32_t before = EditorSession_GetAssetCount(m_Session);
    const std::string missing =
        Engine::Content::PathToUtf8(m_SourceDir / "missing.png");
    EXPECT_EQ(EditorSession_ImportAsset(m_Session, missing.c_str(), 0), -1);
    EXPECT_NE(SessionError(m_Session).find("source file not found"),
              std::string::npos)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), before);
    EXPECT_FALSE(fs::exists(CurrentPath()));
}

TEST_F(ImportServiceTest, ReattachRequiresExplicitCall) {
    RemoveManifestEntry(kPlayerTextureGuid);
    Open();
    EXPECT_NE(SessionWarnings(m_Session).find("sprite GUID missing in registry"),
              std::string::npos)
        << SessionWarnings(m_Session);

    // 普通导入同内容资产：分配新 GUID，不会自动挂接缺失 GUID。
    const std::string bytes =
        ReadFile(RepoRoot() / "assets/gp01/tex/pad.png");
    const std::string unrelated = SourceFile("unrelated.png", bytes);
    ASSERT_GE(EditorSession_ImportAsset(m_Session, unrelated.c_str(), 0), 0)
        << SessionError(m_Session);
    Reopen();
    EXPECT_NE(SessionWarnings(m_Session).find("sprite GUID missing in registry"),
              std::string::npos)
        << "missing GUID must not auto-reattach by content match";

    // 显式 reattach：指定缺失 GUID + 源文件。
    const std::string fix = SourceFile("player_fix.png", bytes);
    ASSERT_EQ(EditorSession_ReattachAsset(m_Session, kPlayerTextureGuid,
                                          fix.c_str(), 0),
              0)
        << SessionError(m_Session);
    Reopen();
    EXPECT_EQ(SessionWarnings(m_Session).find("sprite GUID missing in registry"),
              std::string::npos)
        << "explicit reattach must resolve the missing GUID";
    const int32_t reattached =
        FindAssetByGuid(m_Session, kPlayerTextureGuid);
    ASSERT_GE(reattached, 0);
    EXPECT_EQ(AssetPath(m_Session, reattached),
              "assets/imported/tex/player_fix.png");
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
}

TEST_F(ImportServiceTest, ScriptSaveDoesNotPublishRevision) {
    Open();
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    const std::string currentBefore = ReadFile(CurrentPath());

    int32_t scriptIndex = -1;
    for (int32_t i = 0; i < EditorSession_GetAssetCount(m_Session); ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 1) {
            scriptIndex = i;
            break;
        }
    ASSERT_GE(scriptIndex, 0);
    const std::string path = AssetPath(m_Session, scriptIndex);
    const std::string updated = ReadFile(m_RootPath / path) + "\n-- m1e\n";
    ASSERT_EQ(EditorSession_ScriptSave(m_Session, scriptIndex,
                                       updated.c_str()),
              0)
        << SessionError(m_Session);
    EXPECT_EQ(ReadFile(m_RootPath / path), updated);
    EXPECT_EQ(ReadFile(CurrentPath()), currentBefore)
        << "ScriptSave must not publish a Manifest revision";
}

#if defined(_WIN32)
TEST_F(ImportServiceTest, CommitFailureDoesNotReportImportSuccess) {
    Open();
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    const std::string currentBefore = ReadFile(CurrentPath());
    const int32_t before = EditorSession_GetAssetCount(m_Session);

    struct ScopedHandle {
        HANDLE h = INVALID_HANDLE_VALUE;
        ~ScopedHandle() {
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
    } guard;
    guard.h = CreateFileW(CurrentPath().wstring().c_str(), GENERIC_READ, 0,
                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                          nullptr);
    ASSERT_NE(guard.h, INVALID_HANDLE_VALUE);

    const std::string source =
        SourceFile("blocked.png", "blocked-content");
    EXPECT_EQ(EditorSession_ImportAsset(m_Session, source.c_str(), 0), -1);
    EXPECT_NE(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos)
        << SessionError(m_Session);
    LARGE_INTEGER lockedSize = {};
    ASSERT_TRUE(GetFileSizeEx(guard.h, &lockedSize));
    EXPECT_EQ(lockedSize.QuadPart,
              static_cast<LONGLONG>(currentBefore.size()));
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), before)
        << "failed import must not appear in the live registry";

    CloseHandle(guard.h);
    guard.h = INVALID_HANDLE_VALUE;
    Reopen();
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), before);
    EXPECT_EQ(FindAssetByPath(m_Session, "assets/imported/tex/blocked.png"),
              -1);
}
#endif // _WIN32
