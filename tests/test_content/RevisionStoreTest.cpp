/**
 * @file RevisionStoreTest.cpp
 * @brief Content v2 M1-D：RevisionStore 候选校验与编号不复用门禁
 */

#include <gtest/gtest.h>
#include "Engine/Core/Content/ProjectPaths.h"
#include "Engine/Core/Content/RevisionStore.h"
#include "Engine/Core/Content/SceneSerializerV1.h"

#include <atomic>
#include <chrono>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#endif
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using Engine::Content::ProjectRoot;
using Engine::Content::RevisionOpenState;
using Engine::Content::RevisionStore;

namespace {

std::atomic<int> g_Unique{0};

constexpr const char* kEmptyManifest = R"({"version":1,"assets":[]})";

std::string EmptyScene() {
    Engine::Content::SceneSnapshot snap;
    return Engine::Content::SerializeSnapshot(snap).dump(2);
}

void WriteRaw(const fs::path& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << body;
}

std::string ReadRaw(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

} // namespace

class RevisionStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_Dir = fs::temp_directory_path() /
                ("engine_m1d_store_" + std::to_string(stamp) + "_" +
                 std::to_string(g_Unique.fetch_add(1)));
        std::error_code ec;
        fs::create_directories(m_Dir, ec);
        ASSERT_FALSE(ec) << ec.message();
        m_Root = fs::canonical(m_Dir, ec);
        ASSERT_FALSE(ec) << ec.message();
        Engine::Content::PathError error = Engine::Content::PathError::None;
        std::string diagnostic;
        ASSERT_TRUE(ProjectRoot::Create(m_Root.generic_string(), &m_Project,
                                        &error, &diagnostic))
            << Engine::Content::ToString(error) << " " << diagnostic;
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_Dir, ec);
    }

    fs::path CurrentPath() const {
        return m_Root / ".engine-content" / "current";
    }
    fs::path RevisionPath(int64_t revision) const {
        return m_Root / ".engine-content" / "revisions" /
               std::to_string(revision);
    }

    fs::path m_Dir;
    fs::path m_Root;
    ProjectRoot m_Project;
};

TEST_F(RevisionStoreTest, PublishCreatesCurrentAndReadsCommittedScene) {
    RevisionStore store;
    const auto opened = store.Open(m_Project);
    ASSERT_EQ(opened.state, RevisionOpenState::Unversioned);

    const std::string scene = EmptyScene();
    const auto published = store.Publish(m_Project, kEmptyManifest, scene);
    ASSERT_TRUE(published.ok) << published.diagnostic;
    EXPECT_TRUE(published.committed);
    EXPECT_EQ(published.revision, 1);
    EXPECT_TRUE(fs::exists(CurrentPath()));

    RevisionStore reopened;
    const auto current = reopened.Open(m_Project);
    ASSERT_EQ(current.state, RevisionOpenState::Current);
    EXPECT_EQ(current.revision, 1);

    std::string committedScene;
    ASSERT_TRUE(reopened.ReadCommittedScene("ignored", &committedScene));
    EXPECT_EQ(committedScene, scene);
}

TEST_F(RevisionStoreTest, InvalidSceneCandidateKeepsCurrentAndSkipsNumber) {
    RevisionStore store;
    ASSERT_EQ(store.Open(m_Project).state, RevisionOpenState::Unversioned);
    const std::string scene = EmptyScene();
    ASSERT_TRUE(store.Publish(m_Project, kEmptyManifest, scene).ok);

    const std::string currentBefore = ReadRaw(CurrentPath());
    const auto rejected =
        store.Publish(m_Project, kEmptyManifest, "{ broken scene");
    EXPECT_FALSE(rejected.ok);
    EXPECT_FALSE(rejected.committed);
    EXPECT_EQ(ReadRaw(CurrentPath()), currentBefore)
        << "invalid candidate must not replace current";

    // 失败候选目录保留（隔离），编号不复用；模拟重启后仍跳过该编号。
    EXPECT_TRUE(fs::exists(RevisionPath(2)));
    RevisionStore restarted;
    ASSERT_EQ(restarted.Open(m_Project).state, RevisionOpenState::Current);
    const auto next = restarted.Publish(m_Project, kEmptyManifest, scene);
    ASSERT_TRUE(next.ok) << next.diagnostic;
    EXPECT_EQ(next.revision, 3);
}

TEST_F(RevisionStoreTest, InvalidManifestCandidateKeepsCurrent) {
    RevisionStore store;
    ASSERT_EQ(store.Open(m_Project).state, RevisionOpenState::Unversioned);
    const std::string scene = EmptyScene();
    ASSERT_TRUE(store.Publish(m_Project, kEmptyManifest, scene).ok);
    const std::string currentBefore = ReadRaw(CurrentPath());

    const auto rejected =
        store.Publish(m_Project, "{ broken manifest", scene);
    EXPECT_FALSE(rejected.ok);
    EXPECT_EQ(ReadRaw(CurrentPath()), currentBefore);

    RevisionStore reopened;
    ASSERT_EQ(reopened.Open(m_Project).state, RevisionOpenState::Current);
    EXPECT_EQ(reopened.Revision(), 1);
}

TEST_F(RevisionStoreTest, StaleBaselineWriterIsRejected) {
    RevisionStore a;
    RevisionStore b;
    ASSERT_EQ(a.Open(m_Project).state, RevisionOpenState::Unversioned);
    ASSERT_EQ(b.Open(m_Project).state, RevisionOpenState::Unversioned);

    const std::string scene = EmptyScene();
    ASSERT_TRUE(a.Publish(m_Project, kEmptyManifest, scene).ok);

    const auto stale = b.Publish(m_Project, kEmptyManifest, scene);
    EXPECT_FALSE(stale.ok);
    EXPECT_FALSE(stale.recoveryRequired);
    EXPECT_NE(stale.diagnostic.find("stale baseline"), std::string::npos)
        << stale.diagnostic;

    RevisionStore c;
    RevisionStore d;
    ASSERT_EQ(c.Open(m_Project).state, RevisionOpenState::Current);
    ASSERT_EQ(d.Open(m_Project).state, RevisionOpenState::Current);
    ASSERT_TRUE(c.Publish(m_Project, kEmptyManifest, scene).ok);
    const auto staleAfterOpen = d.Publish(m_Project, kEmptyManifest, scene);
    EXPECT_FALSE(staleAfterOpen.ok);
    EXPECT_NE(staleAfterOpen.diagnostic.find("stale baseline"),
              std::string::npos)
        << staleAfterOpen.diagnostic;
}

TEST_F(RevisionStoreTest, VersionedProjectMissingCurrentIsRecoveryRequired) {
    RevisionStore store;
    ASSERT_EQ(store.Open(m_Project).state, RevisionOpenState::Unversioned);
    ASSERT_TRUE(store.Publish(m_Project, kEmptyManifest, EmptyScene()).ok);
    ASSERT_TRUE(fs::exists(m_Root / ".engine-content" / "versioned"));

    std::error_code ec;
    fs::remove(CurrentPath(), ec);
    ASSERT_FALSE(ec) << ec.message();

    RevisionStore reopened;
    const auto state = reopened.Open(m_Project);
    EXPECT_EQ(state.state, RevisionOpenState::RecoveryRequired);
    EXPECT_NE(state.diagnostic.find("missing current"), std::string::npos)
        << state.diagnostic;
}

TEST_F(RevisionStoreTest, MetadataWithoutVersionedMarkerIsLegacyCompatible) {
    std::error_code ec;
    const fs::path rev5 = RevisionPath(5);
    fs::create_directories(rev5, ec);
    ASSERT_FALSE(ec) << ec.message();
    WriteRaw(rev5 / "manifest.json", kEmptyManifest);
    WriteRaw(rev5 / "scene.scene", EmptyScene());

    RevisionStore store;
    ASSERT_EQ(store.Open(m_Project).state, RevisionOpenState::Unversioned)
        << "failed first publish metadata must not poison legacy open";
}

TEST_F(RevisionStoreTest, FirstPublishFailureDoesNotMarkVersioned) {
    std::error_code ec;
    fs::create_directories(m_Root / ".engine-content", ec);
    ASSERT_FALSE(ec) << ec.message();
    const fs::path revisions = m_Root / ".engine-content" / "revisions";
    WriteRaw(revisions, "block");

    RevisionStore store;
    ASSERT_EQ(store.Open(m_Project).state, RevisionOpenState::Unversioned);
    const auto failed = store.Publish(m_Project, kEmptyManifest, EmptyScene());
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(fs::exists(m_Root / ".engine-content" / "versioned"));

    RevisionStore reopened;
    EXPECT_EQ(reopened.Open(m_Project).state, RevisionOpenState::Unversioned);
}

#if defined(_WIN32)
TEST_F(RevisionStoreTest, WriteLockBusyIsNotCommitted) {
    std::error_code ec;
    fs::create_directories(m_Root / ".engine-content", ec);
    ASSERT_FALSE(ec) << ec.message();
    const fs::path lock = m_Root / ".engine-content" / "write.lock";
    HANDLE held = CreateFileW(lock.wstring().c_str(),
                              GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(held, INVALID_HANDLE_VALUE);

    RevisionStore store;
    ASSERT_EQ(store.Open(m_Project).state, RevisionOpenState::Unversioned);
    const auto busy = store.Publish(m_Project, kEmptyManifest, EmptyScene());
    EXPECT_FALSE(busy.ok);
    EXPECT_FALSE(busy.recoveryRequired);
    EXPECT_NE(busy.diagnostic.find("lock"), std::string::npos)
        << busy.diagnostic;
    EXPECT_FALSE(fs::exists(CurrentPath()));

    CloseHandle(held);
    EXPECT_TRUE(store.Publish(m_Project, kEmptyManifest, EmptyScene()).ok);
}
#endif // _WIN32
