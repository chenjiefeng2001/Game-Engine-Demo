/**
 * @file test_m1f_fault_matrix.cpp
 * @brief Content v2 M1-F：V1–V6 全链故障矩阵（真实外部故障注入）
 *
 * 每个用例记录：注入点、提交状态、恢复动作与可执行证据。
 */

#include <gtest/gtest.h>
#include "editor_bridge/capi.h"

#include <Engine/Core/Content/AtomicFile.h>
#include <Engine/Core/Content/RevisionStore.h>
#include <Engine/Core/Content/Utf8Path.h>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#else
#include <sys/wait.h>
#endif

#ifndef M1F_CRASH_HARNESS
#error "M1F_CRASH_HARNESS must be defined by CMake"
#endif

#include <algorithm>
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

struct CapturedEvent {
    int32_t type = 0;
    std::string payload;
};

void CaptureEvent(int32_t type, const char* payload, void* userData) {
    auto* events = static_cast<std::vector<CapturedEvent>*>(userData);
    events->push_back({type, payload ? payload : ""});
}

bool HasEvent(const std::vector<CapturedEvent>& events, int32_t type) {
    for (const auto& e : events)
        if (e.type == type) return true;
    return false;
}

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

int32_t FindAsset(EditorSessionHandle session, const std::string& path) {
    for (int32_t i = 0; i < EditorSession_GetAssetCount(session); ++i)
        if (AssetPath(session, i) == path) return i;
    return -1;
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

} // namespace

class FaultMatrixTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
        m_OldCwd = fs::current_path();
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_Scratch = fs::temp_directory_path() /
                    ("engine_m1f_" + std::to_string(stamp) + "_" +
                     std::to_string(g_Unique.fetch_add(1)));
        m_SourceDir = fs::temp_directory_path() /
                      ("engine_m1f_src_" + std::to_string(stamp) + "_" +
                       std::to_string(g_Unique.fetch_add(1)));
        std::error_code ec;
        fs::create_directories(m_Scratch / "assets", ec);
        ASSERT_FALSE(ec) << ec.message();
        fs::create_directories(m_Scratch / "empty-cwd", ec);
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
        EditorSession_SetRevisionFaultHookForTesting(nullptr, nullptr);
        EditorSession_SetContentFaultHookForTesting(nullptr, nullptr);
        EditorSession_SetAtomicFaultHookForTesting(nullptr, nullptr);
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

    void BlockRevisionsDir() {
        std::error_code ec;
        fs::create_directories(m_RootPath / ".engine-content", ec);
        ASSERT_FALSE(ec) << ec.message();
        WriteFile(m_RootPath / ".engine-content" / "revisions", "blocked");
    }

    void UnblockRevisionsDir() {
        std::error_code ec;
        const fs::path revisions =
            m_RootPath / ".engine-content" / "revisions";
        fs::remove(revisions, ec);
        ASSERT_FALSE(ec) << ec.message();
        fs::create_directories(revisions, ec);
        ASSERT_FALSE(ec) << ec.message();
    }

    std::string SourceFile(const std::string& name, const std::string& bytes) {
        const fs::path path = m_SourceDir / name;
        WriteFile(path, bytes);
        return Engine::Content::PathToUtf8(path);
    }

    fs::path CurrentPath() const {
        return m_RootPath / ".engine-content" / "current";
    }

    void DestroySession() {
        if (m_Session) {
            EditorSession_Destroy(m_Session);
            m_Session = nullptr;
        }
    }

    int RunHarness(const std::string& mode, const std::string& point) {
        DestroySession();
#ifdef _WIN32
        std::wstring exe =
            Engine::Content::PathFromUtf8(M1F_CRASH_HARNESS).wstring();
        std::wstring root = Engine::Content::PathFromUtf8(m_Root).wstring();
        std::wstring modeW(mode.begin(), mode.end());
        std::wstring pointW(point.begin(), point.end());
        const wchar_t* argv[] = {exe.c_str(), modeW.c_str(), root.c_str(),
                                 pointW.c_str(), nullptr};
        const intptr_t rc = _wspawnv(_P_WAIT, exe.c_str(), argv);
        return static_cast<int>(rc);
#else
        const std::string command = "\"" +
            std::string(M1F_CRASH_HARNESS) + "\" " + mode + " \"" +
            m_Root + "\" " + point + " > /dev/null 2>&1";
        const int rc = std::system(command.c_str());
        return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
    }

    fs::path m_OldCwd;
    fs::path m_Scratch;
    fs::path m_SourceDir;
    fs::path m_RootPath;
    std::string m_Root;
    EditorSessionHandle m_Session = nullptr;
};

// V1：提交前失败不得清 dirty、不得发 Saved、不得建立 current。
TEST_F(FaultMatrixTest, V1_SavePreCommitFailureKeepsDirtyAndNoSaved) {
    Open();
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "V1Probe"), 10);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 1);
    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureEvent, &events);

    const std::string manifestBefore = ReadFile(m_RootPath / kManifest);
    BlockRevisionsDir();

    EXPECT_EQ(EditorSession_SaveProject(m_Session), 0);
    EXPECT_NE(SessionError(m_Session).find("NotCommitted"), std::string::npos)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 1)
        << "failed save must not clear dirty";
    EXPECT_FALSE(HasEvent(events, EV_PROJECT_SAVED));
    EXPECT_FALSE(fs::exists(CurrentPath()));
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 11);
    EXPECT_EQ(ReadFile(m_RootPath / kManifest), manifestBefore);

    // 恢复动作：修复候选目录后重试，提交可得。
    UnblockRevisionsDir();
    events.clear();
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 0);
    EXPECT_TRUE(HasEvent(events, EV_PROJECT_SAVED));
    ASSERT_TRUE(fs::exists(CurrentPath()));
    EXPECT_EQ(nlohmann::json::parse(ReadFile(CurrentPath()))["revision"]
                  .get<int64_t>(),
              1);

    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 11);
}

// V1：导入在源拷贝后、候选准备期失败 → 删除副本、不注册、不发事件。
TEST_F(FaultMatrixTest, V1_ImportPreCommitFailureRemovesCopy) {
    Open();
    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureEvent, &events);
    const int32_t before = EditorSession_GetAssetCount(m_Session);
    const std::string source = SourceFile("fault_import.png", "payload");
    BlockRevisionsDir();

    EXPECT_EQ(EditorSession_ImportAsset(m_Session, source.c_str(), 0), -1);
    EXPECT_NE(SessionError(m_Session).find("NotCommitted"), std::string::npos)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), before);
    EXPECT_FALSE(HasEvent(events, EV_ASSET_IMPORTED));
    EXPECT_FALSE(fs::exists(m_RootPath / "assets" / "imported" / "tex" /
                            "fault_import.png"))
        << "prepared copy must be removed on NotCommitted";
    EXPECT_FALSE(fs::exists(CurrentPath()));

    UnblockRevisionsDir();
    const int32_t index =
        EditorSession_ImportAsset(m_Session, source.c_str(), 0);
    ASSERT_GE(index, 0) << SessionError(m_Session);
    EXPECT_TRUE(fs::exists(m_RootPath / "assets" / "imported" / "tex" /
                           "fault_import.png"));
    EXPECT_TRUE(HasEvent(events, EV_ASSET_IMPORTED));
    EXPECT_TRUE(fs::exists(CurrentPath()));
}

// V2/V4：提交后清理失败 → CommittedCleanupPending；冷启动仍看到新 revision/GUID。
TEST_F(FaultMatrixTest, V2_RenameCleanupPendingColdStartKeepsNewRevisionAndGuid) {
    Open();
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    const std::string oldRel = "assets/gp01/tex/pad.png";
    const int32_t index = FindAsset(m_Session, oldRel);
    ASSERT_GE(index, 0);
    const std::string guid = AssetGuid(m_Session, index);
    const fs::path oldAbs = m_RootPath / oldRel;

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

    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureEvent, &events);
    ASSERT_EQ(EditorSession_RenameAsset(m_Session, index, "pad_pending"), 0)
        << SessionError(m_Session);
    EXPECT_TRUE(fs::exists(oldAbs)) << "cleanup failure keeps old source";
    EXPECT_TRUE(fs::exists(m_RootPath / "assets/gp01/tex/pad_pending.png"));
    EXPECT_NE(SessionWarnings(m_Session).find("CommittedCleanupPending"),
              std::string::npos)
        << SessionWarnings(m_Session);
    EXPECT_TRUE(HasEvent(events, EV_ASSET_RENAMED));
    EXPECT_FALSE(HasEvent(events, EV_PROJECT_SAVED));
    EXPECT_EQ(AssetGuid(m_Session, index), guid);
    EXPECT_EQ(AssetPath(m_Session, index),
              "assets/gp01/tex/pad_pending.png");
    const auto current = nlohmann::json::parse(ReadFile(CurrentPath()));
    EXPECT_EQ(current["revision"].get<int64_t>(), 2);

    CloseHandle(guard.h);
    guard.h = INVALID_HANDLE_VALUE;
    Reopen();
    const int32_t reopened =
        FindAsset(m_Session, "assets/gp01/tex/pad_pending.png");
    ASSERT_GE(reopened, 0);
    EXPECT_EQ(AssetGuid(m_Session, reopened), guid);
    EXPECT_TRUE(fs::exists(oldAbs))
        << "recoverable old copy remains until explicit cleanup";
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
}

// V4/K4.6：大小写-only rename 冷启动后仍保持 GUID 与目标大小写。
TEST_F(FaultMatrixTest, V4_CaseOnlyRenameSurvivesColdStart) {
    Open();
    const int32_t index =
        FindAsset(m_Session, "assets/gp01/tex/pad.png");
    ASSERT_GE(index, 0);
    const std::string guid = AssetGuid(m_Session, index);
    ASSERT_EQ(EditorSession_RenameAsset(m_Session, index, "PAD"), 0)
        << SessionError(m_Session);
    EXPECT_TRUE(DirectoryHasExactName(m_RootPath / "assets/gp01/tex",
                                      "PAD.png"));
    EXPECT_FALSE(DirectoryHasTempArtifacts(m_RootPath / "assets/gp01/tex"));

    Reopen();
    EXPECT_TRUE(DirectoryHasExactName(m_RootPath / "assets/gp01/tex",
                                      "PAD.png"));
    const int32_t reopened =
        FindAsset(m_Session, "assets/gp01/tex/PAD.png");
    ASSERT_GE(reopened, 0);
    EXPECT_EQ(AssetGuid(m_Session, reopened), guid);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
}

// V5：版本化工程从外部 CWD 打开，路径与内容不漂移。
TEST_F(FaultMatrixTest, V5_VersionedOpenFromForeignCwd) {
    Open();
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    EditorSession_Destroy(m_Session);
    m_Session = nullptr;

    std::error_code ec;
    fs::current_path(m_Scratch / "empty-cwd", ec);
    ASSERT_FALSE(ec) << ec.message();
    m_Session = EditorSession_Create();
    ASSERT_NE(m_Session, nullptr);
    ASSERT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              1)
        << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
    EXPECT_GE(FindAsset(m_Session, "assets/gp01/tex/pad.png"), 0);
    EXPECT_FALSE(fs::exists(m_Scratch / "empty-cwd" / "assets"));
}

// V6：旧格式可读；首次保存建立 current 且不升 schema、不改 GUID。
TEST_F(FaultMatrixTest, V6_LegacyReadableAndFirstPublishSchemaStable) {
    Open();
    EXPECT_FALSE(fs::exists(m_RootPath / ".engine-content"));
    const int32_t pad = FindAsset(m_Session, "assets/gp01/tex/pad.png");
    ASSERT_GE(pad, 0);
    const std::string guid = AssetGuid(m_Session, pad);

    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    ASSERT_TRUE(fs::exists(CurrentPath()));
    const auto legacy = nlohmann::json::parse(ReadFile(m_RootPath / kManifest));
    EXPECT_EQ(legacy["version"].get<int>(), 1)
        << "schema must not silently bump";
    EXPECT_NE(ReadFile(m_RootPath / kManifest).find(guid), std::string::npos);

    Reopen();
    const int32_t reopened =
        FindAsset(m_Session, "assets/gp01/tex/pad.png");
    ASSERT_GE(reopened, 0);
    EXPECT_EQ(AssetGuid(m_Session, reopened), guid);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
}

// Reattach：提交前失败保持缺失 GUID；恢复后身份一致并可冷启动。
TEST_F(FaultMatrixTest, ReattachCommitFailureKeepsMissingGuidThenRecovers) {
    // 先移除 player.png 的 manifest 条目，制造缺失 GUID。
    const fs::path manifest = m_RootPath / kManifest;
    nlohmann::json j = nlohmann::json::parse(ReadFile(manifest));
    auto& assets = j["assets"];
    for (auto it = assets.begin(); it != assets.end(); ++it) {
        if ((*it)["guid"].get<std::string>() ==
            "d1000000000000000000000000000001") {
            assets.erase(it);
            break;
        }
    }
    WriteFile(manifest, j.dump(2));
    Open();
    EXPECT_NE(SessionWarnings(m_Session).find("sprite GUID missing in registry"),
              std::string::npos);

    const std::string source = SourceFile("reattach_fix.png", "bytes");
    BlockRevisionsDir();
    EXPECT_EQ(EditorSession_ReattachAsset(
                  m_Session, "d1000000000000000000000000000001",
                  source.c_str(), 0),
              -1);
    EXPECT_FALSE(fs::exists(CurrentPath()));
    EXPECT_FALSE(fs::exists(m_RootPath / "assets/imported/tex/reattach_fix.png"))
        << "prepared reattach copy must be removed on NotCommitted";

    UnblockRevisionsDir();
    ASSERT_EQ(EditorSession_ReattachAsset(
                  m_Session, "d1000000000000000000000000000001",
                  source.c_str(), 0),
              0)
        << SessionError(m_Session);
    Reopen();
    EXPECT_EQ(SessionWarnings(m_Session).find("sprite GUID missing in registry"),
              std::string::npos);
    EXPECT_GE(FindAsset(m_Session,
                        "assets/imported/tex/reattach_fix.png"),
              0);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
}

// RF-1：current 替换成功后回读不匹配 → RecoveryRequired，并封锁受保护操作。
TEST_F(FaultMatrixTest, RF1_ReadbackMismatchEntersRecoveryAndBlocksWrites) {
    Open();
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    const std::string baseline = ReadFile(CurrentPath());
    bool fired = false;

    struct Rf1Context {
        fs::path current;
        std::string baseline;
        bool* fired;
    } rf1{CurrentPath(), baseline, &fired};
    EditorSession_SetRevisionFaultHookForTesting(
        [](int32_t point, void* userData) {
            auto* ctx = static_cast<Rf1Context*>(userData);
            if (point != static_cast<int32_t>(
                             Engine::Content::RevisionFaultPoint::
                                 AfterCurrentReplaceBeforeReadback) ||
                *ctx->fired) {
                return;
            }
            *ctx->fired = true;
            // 让持久化 current 在替换后、回读前回到旧基线，制造回读不一致。
            WriteFile(ctx->current, ctx->baseline);
        },
        &rf1);

    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "RF1Probe"), 10);
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 0);
    EditorSession_SetRevisionFaultHookForTesting(nullptr, nullptr);
    ASSERT_TRUE(fired);
    EXPECT_NE(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos)
        << SessionError(m_Session);
    EXPECT_EQ(ReadFile(CurrentPath()), baseline);

    // 受保护操作全部拒绝。
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 0);
    EXPECT_NE(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos);
    EXPECT_EQ(EditorSession_RenameAsset(m_Session, 0, "blocked"), -1);
    EXPECT_EQ(EditorSession_ImportAsset(
                  m_Session,
                  SourceFile("rf1.png", "x").c_str(), 0),
              -1);
    EXPECT_EQ(EditorSession_Play(m_Session, -1), 0);
    EXPECT_EQ(EditorSession_Reload(m_Session), 0);

    int32_t scriptIndex = -1;
    for (int32_t i = 0; i < EditorSession_GetAssetCount(m_Session); ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 1) {
            scriptIndex = i;
            break;
        }
    ASSERT_GE(scriptIndex, 0);
    EXPECT_EQ(EditorSession_ScriptSave(m_Session, scriptIndex, "-- blocked\n"),
              -1);

    // 重启后仍只看到旧完整 revision。
    Reopen();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
}

// RF-3：ScriptSave temp write / verify / replace 三段失败均保留原文件且不改 revision。
TEST_F(FaultMatrixTest, RF3_ScriptSaveStageFailuresKeepOriginal) {
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
    const std::string rel = AssetPath(m_Session, scriptIndex);
    const fs::path abs = m_RootPath / rel;
    const std::string original = ReadFile(abs);

    using Engine::Content::AtomicFaultPoint;
    const AtomicFaultPoint points[] = {
        AtomicFaultPoint::BeforeTempWrite,
        AtomicFaultPoint::AfterTempWriteBeforeVerify,
        AtomicFaultPoint::BeforeReplace,
    };
    for (AtomicFaultPoint point : points) {
        struct Rf3Context {
            int32_t point;
        } rf3{static_cast<int32_t>(point)};
        EditorSession_SetAtomicFaultHookForTesting(
            [](int32_t p, const char* tempPath, const char*, void* userData) {
                auto* ctx = static_cast<Rf3Context*>(userData);
                if (p != ctx->point) return 0;
                if (p == static_cast<int32_t>(
                             AtomicFaultPoint::AfterTempWriteBeforeVerify)) {
                    WriteFile(fs::path(tempPath), "corrupted-temp");
                    return 0;
                }
                return 1;   // BeforeTempWrite / BeforeReplace: 注入失败
            },
            &rf3);
        EXPECT_EQ(EditorSession_ScriptSave(m_Session, scriptIndex,
                                           "-- injected\n"),
                  -1)
            << "point=" << static_cast<int>(point);
        EditorSession_SetAtomicFaultHookForTesting(nullptr, nullptr);
        EXPECT_EQ(ReadFile(abs), original)
            << "point=" << static_cast<int>(point);
        EXPECT_EQ(ReadFile(CurrentPath()), currentBefore)
            << "point=" << static_cast<int>(point);
        EXPECT_FALSE(DirectoryHasTempArtifacts(abs.parent_path()));
    }
}

// RF-2：current 替换前终止 → 重启只看到旧完整 revision。
TEST_F(FaultMatrixTest, RF2_CrashBeforeCurrentReplaceKeepsOldRevision) {
    Open();
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    DestroySession();

    ASSERT_EQ(RunHarness("save", "BeforeCurrentReplace"), 90);
    Open();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
    EXPECT_EQ(nlohmann::json::parse(ReadFile(CurrentPath()))["revision"]
                  .get<int64_t>(),
              1);
}

// RF-2：current 替换后终止 → 重启看到新完整 revision（K1.6）。
TEST_F(FaultMatrixTest, RF2_CrashAfterCurrentReplaceSeesNewRevision) {
    Open();
    ASSERT_EQ(EditorSession_SaveProject(m_Session), 1)
        << SessionError(m_Session);
    DestroySession();

    ASSERT_EQ(RunHarness("save", "AfterCurrentReplaceBeforeReadback"), 90);
    Open();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 11);
    EXPECT_EQ(nlohmann::json::parse(ReadFile(CurrentPath()))["revision"]
                  .get<int64_t>(),
              2);
}

// RF-2：首次发布在 current 前终止（标记已写）→ 重启 RecoveryRequired。
TEST_F(FaultMatrixTest, RF2_CrashFirstPublishBeforeCurrentIsRecoveryRequired) {
    ASSERT_EQ(RunHarness("first-save", "BeforeCurrentReplace"), 90);
    EXPECT_FALSE(fs::exists(CurrentPath()));
    EXPECT_TRUE(fs::exists(m_RootPath / ".engine-content" / "versioned"));

    m_Session = EditorSession_Create();
    ASSERT_NE(m_Session, nullptr);
    EXPECT_EQ(EditorSession_OpenProjectRooted(m_Session, m_Root.c_str(),
                                              kManifest, kScene),
              0);
    EXPECT_NE(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos)
        << SessionError(m_Session);
}

// RF-2：首次发布在候选校验后、标记前终止 → 候选不晋升，旧格式可读。
TEST_F(FaultMatrixTest, RF2_CrashFirstPublishCandidateDoesNotPromote) {
    ASSERT_EQ(RunHarness("first-save", "AfterCandidateValidation"), 90);
    EXPECT_FALSE(fs::exists(CurrentPath()));
    EXPECT_FALSE(fs::exists(m_RootPath / ".engine-content" / "versioned"));
    EXPECT_TRUE(fs::exists(m_RootPath / ".engine-content" / "revisions" / "1"));

    Open();
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 10);
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), 33);
}

// RF-2：rename 提交后、旧源清理前终止 → 新 current 可用，旧源保留。
TEST_F(FaultMatrixTest, RF2_CrashDuringRenameCleanupKeepsNewRevision) {
    Open();
    const int32_t index =
        FindAsset(m_Session, "assets/gp01/tex/pad.png");
    ASSERT_GE(index, 0);
    const std::string guid = AssetGuid(m_Session, index);
    DestroySession();

    ASSERT_EQ(RunHarness("rename", "BeforeRenameSourceCleanup"), 90);
    Open();
    const int32_t reopened =
        FindAsset(m_Session, "assets/gp01/tex/pad_crash.png");
    ASSERT_GE(reopened, 0);
    EXPECT_EQ(AssetGuid(m_Session, reopened), guid);
    EXPECT_TRUE(fs::exists(m_RootPath / "assets/gp01/tex/pad.png"))
        << "interrupted cleanup leaves old source as recoverable copy";
    EXPECT_EQ(nlohmann::json::parse(ReadFile(CurrentPath()))["revision"]
                  .get<int64_t>(),
              1);
}
