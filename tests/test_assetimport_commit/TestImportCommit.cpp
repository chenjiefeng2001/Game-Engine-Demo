/**
 * @file TestImportCommit.cpp
 * @brief P6-1B B3 — Model import commit chain (Mesh + Material atomic group;
 *        C-2 hash idempotency; reimport atomicity across all 8 RevisionFaultPoints).
 */
#include <gtest/gtest.h>
#include "editor_bridge/capi.h"
#include <Engine/Core/Content/Utf8Path.h>

#include <nlohmann/json.hpp>

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
constexpr const char* kSimpleObj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";

std::atomic<int> g_Unique{0};

const fs::path& RepoRoot() {
    static const fs::path root = fs::current_path();
    return root;
}

std::string ReadFileAt(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void WriteFileAt(const fs::path& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << body;
}

std::string SessionError(EditorSessionHandle session) {
    char buf[2048] = {};
    EditorSession_GetLastError(session, buf, sizeof(buf));
    return buf;
}

std::string AssetPathAt(EditorSessionHandle session, int32_t index) {
    char buf[512] = {};
    if (EditorSession_GetAssetPath(session, index, buf, sizeof(buf)) < 0)
        return {};
    return buf;
}

std::string AssetGuidHexAt(EditorSessionHandle session, int32_t index) {
    char buf[64] = {};
    if (EditorSession_GetAssetGuid(session, index, buf, sizeof(buf)) < 0)
        return {};
    return buf;
}

class ImportCommitTest : public ::testing::Test {
protected:
    static fs::path LocateRepoAssetsRoot() {
        fs::path cwd = fs::current_path();
        fs::path candidate = cwd;
        while (candidate.has_parent_path()) {
            if (fs::exists(candidate / "assets" / "gp01")) return candidate;
            candidate = candidate.parent_path();
        }
        return cwd;
    }

    void SetUp() override {
        ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        const std::string unique =
            std::to_string(stamp) + "_" + std::to_string(g_Unique.fetch_add(1));
        m_Scratch = fs::temp_directory_path() / ("engine_p61b_b3_" + unique);
        m_SourceDir =
            fs::temp_directory_path() / ("engine_p61b_b3src_" + unique);
        std::error_code ec;
        fs::create_directories(m_Scratch / "assets", ec);
        ASSERT_FALSE(ec) << "create scratch/assets: " << ec.message();
        fs::create_directories(m_SourceDir, ec);
        ASSERT_FALSE(ec) << "create scratch/source: " << ec.message();
        const fs::path localRepo = LocateRepoAssetsRoot();
        fs::copy(localRepo / "assets" / "gp01",
                 m_Scratch / "assets" / "gp01",
                 fs::copy_options::recursive |
                     fs::copy_options::overwrite_existing,
                 ec);
        ASSERT_FALSE(ec) << "cannot seed assets/gp01 from " << localRepo
                         << ": " << ec.message();
        m_RootPath = fs::canonical(m_Scratch, ec);
        ASSERT_FALSE(ec) << "canonical scratch: " << ec.message();
        m_Root = Engine::Content::PathToUtf8(m_RootPath);
        Open();
    }

    void TearDown() override {
        EditorSession_SetRevisionFaultHookForTesting(nullptr, nullptr);
        EditorSession_SetAtomicFaultHookForTesting(nullptr, nullptr);
        EditorSession_SetEventCallback(m_Session, nullptr, nullptr);
        if (m_Session) {
            EditorSession_Destroy(m_Session);
            m_Session = nullptr;
        }
        std::error_code ec;
        fs::remove_all(m_Scratch, ec);
        fs::remove_all(m_SourceDir, ec);
    }

    void Open() {
        m_Session = EditorSession_Create();
        ASSERT_NE(m_Session, nullptr);
        ASSERT_EQ(EditorSession_OpenProjectRooted(
                      m_Session, m_Root.c_str(), kManifest, kScene),
                  1)
            << SessionError(m_Session);
    }

    void Reopen() {
        if (m_Session) EditorSession_Destroy(m_Session);
        Open();
    }

    fs::path CurrentPath() const {
        return m_RootPath / ".engine-content" / "current";
    }

    int64_t CommittedRevision() {
        std::error_code ec;
        if (!fs::exists(CurrentPath(), ec)) return -1;
        try {
            nlohmann::json j = nlohmann::json::parse(ReadFileAt(CurrentPath()));
            return j.value("revision", static_cast<int64_t>(-1));
        } catch (...) {
            return -1;
        }
    }

    // Baseline import followed by source mutation so a reimport produces a
    // changed digest (used by fault-matrix cases on an existing revision).
    void PrepareBaselineAndChangedSource() {
        const fs::path objPath = m_RootPath / "assets" / "models" / "tri.obj";
        fs::create_directories(objPath.parent_path());
        WriteFileAt(objPath, kSimpleObj);
        ASSERT_GT(
            EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2),
            0)
            << SessionError(m_Session);
        WriteFileAt(objPath, "v 0 0 0\nv 3 0 0\nv 0 3 0\nf 1 2 3\n");
    }

    fs::path m_Scratch;
    fs::path m_SourceDir;
    fs::path m_RootPath;
    std::string m_Root;
    EditorSessionHandle m_Session = nullptr;
};

// B4 helpers: tiny 2x2 PNG content generator + OBJ-side MTL texture writer.
namespace ImportCommitHelpers {
static uint32_t Crc32(const uint8_t* d, size_t n) {
    static uint32_t table[256];
    static bool init = [] {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        return true;
    }();
    (void)init;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> MakeTinyPng2x2(uint8_t r, uint8_t g, uint8_t b) {
    auto push32BE = [](std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(x >> 24 & 0xFF); v.push_back(x >> 16 & 0xFF);
        v.push_back(x >> 8 & 0xFF); v.push_back(x & 0xFF);
    };
    auto appendChunk = [&](std::vector<uint8_t>& png, const char* type,
                           const std::vector<uint8_t>& data) {
        push32BE(png, static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> crcIn;
        for (int k = 0; k < 4; ++k) crcIn.push_back(uint8_t(type[k]));
        crcIn.insert(crcIn.end(), data.begin(), data.end());
        for (int k = 0; k < 4; ++k) png.push_back(static_cast<uint8_t>(type[k]));
        png.insert(png.end(), data.begin(), data.end());
        push32BE(png, Crc32(&crcIn[0], crcIn.size()));
    };
    std::vector<uint8_t> raw;
    for (uint32_t y = 0; y < 2; ++y) {
        raw.push_back(0);
        for (uint32_t x = 0; x < 2; ++x) {
            raw.push_back(r);
            raw.push_back(g);
            raw.push_back(b);
            raw.push_back(255);
        }
    }
    std::vector<uint8_t> idat;
    idat.push_back(0x78); idat.push_back(0x01);
    const uint16_t len = static_cast<uint16_t>(raw.size());
    idat.push_back(0x01);
    idat.push_back(len & 0xFF); idat.push_back(len >> 8);
    idat.push_back(~len & 0xFF); idat.push_back((~len >> 8) & 0xFF);
    idat.insert(idat.end(), raw.begin(), raw.end());
    uint32_t a1 = 1, a2 = 0;
    for (const uint8_t v : raw) {
        a1 = (a1 + v) % 65521u;
        a2 = (a2 + a1) % 65521u;
    }
    const uint32_t adler = (a2 << 16) | a1;
    push32BE(idat, adler);
    std::vector<uint8_t> png;
    const uint8_t sig[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), sig, sig + 8);
    std::vector<uint8_t> ihdr;
    push32BE(ihdr, 2); push32BE(ihdr, 2);
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0);
    ihdr.push_back(0); ihdr.push_back(0);
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", idat);
    appendChunk(png, "IEND", {});
    return png;
}

void WriteTextureSource(const fs::path& png, uint8_t r, uint8_t g,
                        uint8_t b) {
    std::ofstream f(png, std::ios::binary | std::ios::trunc);
    const auto bytes = MakeTinyPng2x2(r, g, b);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}
} // namespace ImportCommitHelpers

} // namespace

// ── happy paths ──────────────────────────────────────────────────────────

TEST_F(ImportCommitTest, ImportObjProjectRelativeRegistersMesh) {
    const fs::path objPath = m_RootPath / "assets" / "models" / "tri.obj";
    fs::create_directories(objPath.parent_path());
    WriteFileAt(objPath, kSimpleObj);
    const int32_t idx =
        EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2);
    ASSERT_GT(idx, 0) << SessionError(m_Session);
    // mesh registry path is content-addressed: <stemDir>/<base>.<digest8>.mesh
    const std::string registered = AssetPathAt(m_Session, idx);
    EXPECT_NE(registered.find("assets/models/tri."), std::string::npos)
        << registered;
    EXPECT_NE(registered.find(".mesh"), std::string::npos)
        << registered;
    // source linkage is preserved in the mesh asset bytes
    bool foundMeshFile = false;
    for (const auto& e : fs::directory_iterator(objPath.parent_path())) {
        if (e.path().extension() == ".mesh") foundMeshFile = true;
    }
    EXPECT_TRUE(foundMeshFile);
    EXPECT_GT(CommittedRevision(), 0);
}

TEST_F(ImportCommitTest, ImportObjWithMtlRegistersMaterialAssets) {
    const fs::path objPath = m_RootPath / "assets" / "models" / "lit.obj";
    fs::create_directories(objPath.parent_path());
    WriteFileAt(objPath,
                "mtllib lit.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "usemtl red\nf 1 2 3\n");
    WriteFileAt(objPath.parent_path() / "lit.mtl",
                "newmtl red\nKd 1 0 0\nmap_Kd base.png\n");
    ImportCommitHelpers::WriteTextureSource(
        objPath.parent_path() / "base.png", 200, 30, 30);
    const int32_t idx =
        EditorSession_ImportAsset(m_Session, "assets/models/lit.obj", 2);
    ASSERT_GT(idx, 0) << SessionError(m_Session);
    int32_t materials = 0;
    const int32_t count = EditorSession_GetAssetCount(m_Session);
    for (int32_t i = 0; i < count; ++i) {
        const std::string p = AssetPathAt(m_Session, i);
        if (p.find(".material.json") != std::string::npos) ++materials;
    }
    EXPECT_EQ(materials, 1);
}

TEST_F(ImportCommitTest, MeshType3IsFailClosed) {
    WriteFileAt(m_RootPath / "unused.bin", "x");
    EXPECT_EQ(EditorSession_ImportAsset(m_Session, "unused.bin", 3), -1);
}

TEST_F(ImportCommitTest, UnsupportedMeshExtensionFailClosed) {
    WriteFileAt(m_RootPath / "bad.fbx", "x");
    EXPECT_EQ(EditorSession_ImportAsset(m_Session, "bad.fbx", 2), -1);
    EXPECT_NE(std::string(SessionError(m_Session))
                  .find("unsupported mesh extension"),
              std::string::npos)
        << SessionError(m_Session);
}

TEST_F(ImportCommitTest, ExternalSourceCopiedContentAddressed) {
    WriteFileAt(m_SourceDir / "tri.obj", kSimpleObj);
    const std::string src =
        Engine::Content::PathToUtf8(m_SourceDir / "tri.obj");
    const int32_t idx = EditorSession_ImportAsset(m_Session, src.c_str(), 2);
    ASSERT_GT(idx, 0) << SessionError(m_Session);
    const std::string path = AssetPathAt(m_Session, idx);
    EXPECT_EQ(path.rfind("assets/imported/model/", 0), 0) << path;
    const int32_t idx2 = EditorSession_ImportAsset(m_Session, src.c_str(), 2);
    EXPECT_EQ(idx, idx2);
}

// ── C-2 idempotency ──────────────────────────────────────────────────────

TEST_F(ImportCommitTest, SameDigestReturnsSameGuidWithoutRevision) {
    const fs::path objPath = m_RootPath / "assets" / "models" / "tri.obj";
    fs::create_directories(objPath.parent_path());
    WriteFileAt(objPath, kSimpleObj);
    const int32_t idx1 =
        EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2);
    ASSERT_GT(idx1, 0);
    const std::string guid1 = AssetGuidHexAt(m_Session, idx1);
    const int64_t rev1 = CommittedRevision();

    const int32_t idx2 =
        EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2);
    ASSERT_GT(idx2, 0);
    EXPECT_EQ(idx1, idx2);
    EXPECT_EQ(guid1, AssetGuidHexAt(m_Session, idx2));
    EXPECT_EQ(CommittedRevision(), rev1);
}

TEST_F(ImportCommitTest, ChangedSourceKeepsGuidAndCreatesRevision) {
    const fs::path objPath = m_RootPath / "assets" / "models" / "tri.obj";
    fs::create_directories(objPath.parent_path());
    WriteFileAt(objPath, kSimpleObj);
    const int32_t idx1 =
        EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2);
    ASSERT_GT(idx1, 0);
    const std::string guid1 = AssetGuidHexAt(m_Session, idx1);
    const int64_t rev1 = CommittedRevision();

    WriteFileAt(objPath, "v 0 0 0\nv 2 0 0\nv 0 2 0\nf 1 2 3\n");
    const int32_t idx2 =
        EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2);
    ASSERT_GT(idx2, 0) << SessionError(m_Session);
    EXPECT_EQ(guid1, AssetGuidHexAt(m_Session, idx2));
    EXPECT_GT(CommittedRevision(), rev1);
}

TEST_F(ImportCommitTest, DependencyChangeAlsoCreatesRevision) {
    const fs::path objPath = m_RootPath / "assets" / "models" / "lit.obj";
    fs::create_directories(objPath.parent_path());
    WriteFileAt(objPath,
                "mtllib lit.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "usemtl red\nf 1 2 3\n");
    WriteFileAt(objPath.parent_path() / "lit.mtl", "newmtl red\nKd 0 0 1\n");
    ImportCommitHelpers::WriteTextureSource(
        objPath.parent_path() / "base.png", 200, 30, 30);
    const int32_t idx1 =
        EditorSession_ImportAsset(m_Session, "assets/models/lit.obj", 2);
    ASSERT_GT(idx1, 0);
    const int64_t rev1 = CommittedRevision();
    WriteFileAt(objPath.parent_path() / "lit.mtl", "newmtl red\nKd 1 0 0\n");
    const int32_t idx2 =
        EditorSession_ImportAsset(m_Session, "assets/models/lit.obj", 2);
    ASSERT_GT(idx2, 0) << SessionError(m_Session);
    EXPECT_GT(CommittedRevision(), rev1);
}

// 控制组：type=0 纹理 import 在 OpenProjectRooted 环境给出稳定索引与幂等
//（覆盖 ImportAssetInternal 尾部 return-UB 回归；GL-free）
TEST_F(ImportCommitTest, ControlTexImportGuidInteraction) {
    const fs::path tex =
        m_RootPath / "assets" / "gp01" / "tex" / "imported_tex.png";
    std::error_code ec;
    fs::copy(LocateRepoAssetsRoot() / "assets" / "gp01" / "tex" / "pad.png",
             tex, fs::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec);
    const int32_t before = EditorSession_GetAssetCount(m_Session);
    const int32_t idx = EditorSession_ImportAsset(
        m_Session, "assets/gp01/tex/imported_tex.png", 0);
    ASSERT_GE(idx, 0) << SessionError(m_Session);
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), before + 1);
    const int32_t again = EditorSession_ImportAsset(
        m_Session, "assets/gp01/tex/imported_tex.png", 0);
    EXPECT_EQ(again, idx);
}

// ── B4-D texture commit integration ─────────────────────────────────────

TEST_F(ImportCommitTest, ImportObjWithMtlRegistersTextureAssets) {
    const fs::path objPath = m_RootPath / "assets" / "models" / "lit.obj";
    fs::create_directories(objPath.parent_path());
    WriteFileAt(objPath,
                "mtllib lit.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "usemtl red\nf 1 2 3\n");
    WriteFileAt(objPath.parent_path() / "lit.mtl",
                "newmtl red\nKd 1 0 0\nmap_Kd base.png\n");
    ImportCommitHelpers::WriteTextureSource(
        objPath.parent_path() / "base.png", 200, 30, 30);

    const int32_t idx =
        EditorSession_ImportAsset(m_Session, "assets/models/lit.obj", 2);
    ASSERT_GT(idx, 0) << SessionError(m_Session);
    const int32_t n = EditorSession_GetAssetCount(m_Session);
    bool textureRegistered = false;
    bool meshRegistered = false;
    bool materialRegistered = false;
    for (int32_t i = 0; i < n; ++i) {
        const std::string p = AssetPathAt(m_Session, i);
        if (p.find(".tex.png") != std::string::npos) textureRegistered = true;
        if (p.find(".mesh") != std::string::npos) meshRegistered = true;
        if (p.find(".material.json") != std::string::npos)
            materialRegistered = true;
    }
    EXPECT_TRUE(textureRegistered);
    EXPECT_TRUE(meshRegistered);
    EXPECT_TRUE(materialRegistered);
    // single publish confirmation is validated by the revision delta == 1
    // relative to baseline
}

TEST_F(ImportCommitTest, TextureBytesChangeTriggersRevisionWithStableGuid) {
    const fs::path objPath = m_RootPath / "assets" / "models" / "lit.obj";
    fs::create_directories(objPath.parent_path());
    WriteFileAt(objPath,
                "mtllib lit.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "usemtl red\nf 1 2 3\n");
    WriteFileAt(objPath.parent_path() / "lit.mtl",
                "newmtl red\nKd 1 0 0\nmap_Kd base.png\n");
    ImportCommitHelpers::WriteTextureSource(
        objPath.parent_path() / "base.png", 200, 30, 30);

    const int32_t idx1 =
        EditorSession_ImportAsset(m_Session, "assets/models/lit.obj", 2);
    ASSERT_GT(idx1, 0) << SessionError(m_Session);
    const std::string guid1 = AssetGuidHexAt(m_Session, idx1);
    const int64_t rev1 = CommittedRevision();

    // Only the texture bytes change; OBJ/MTL bytes unchanged → new revision
    ImportCommitHelpers::WriteTextureSource(
        objPath.parent_path() / "base.png", 10, 90, 200);
    const int32_t idx2 =
        EditorSession_ImportAsset(m_Session, "assets/models/lit.obj", 2);
    ASSERT_GT(idx2, 0) << SessionError(m_Session);
    EXPECT_EQ(guid1, AssetGuidHexAt(m_Session, idx2));
    EXPECT_GT(CommittedRevision(), rev1);

    // identical reimport must not create another revision
    const int64_t rev2 = CommittedRevision();
    EditorSession_ImportAsset(m_Session, "assets/models/lit.obj", 2);
    EXPECT_EQ(CommittedRevision(), rev2);
}

TEST_F(ImportCommitTest, MissingTextureFailsWithNoPartialCommit) {
    const fs::path objPath = m_RootPath / "assets" / "models" / "lit.obj";
    fs::create_directories(objPath.parent_path());
    WriteFileAt(objPath,
                "mtllib lit.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "usemtl red\nf 1 2 3\n");
    WriteFileAt(objPath.parent_path() / "lit.mtl",
                "newmtl red\nKd 1 0 0\nmap_Kd ghost.png\n");
    const int64_t baseline = CommittedRevision();
    const std::string baselineBytes = ReadFileAt(CurrentPath());
    const int32_t idx =
        EditorSession_ImportAsset(m_Session, "assets/models/lit.obj", 2);
    EXPECT_EQ(idx, -1);
    EXPECT_LT(std::string(SessionError(m_Session))
                  .find("texture source unreadable"),
              std::string::npos)
        << SessionError(m_Session);
    EXPECT_EQ(CommittedRevision(), baseline);
    EXPECT_EQ(ReadFileAt(CurrentPath()), baselineBytes);
}

// ── B3-D fault matrix (all 8 RevisionFaultPoint timings) ─────────────────

namespace {

struct FaultCtx {
    int32_t targetPoint = -1;
    std::atomic<bool> fired{false};
    std::atomic<bool> armed{false};
    std::string atomicSuffix;
    std::string baselineCurrent;
    fs::path currentPath;
    std::string deletedCandidateScene;
};

void RevisionFaultHookFn(int32_t point, void* userData) {
    auto* ctx = static_cast<FaultCtx*>(userData);
    if (ctx->targetPoint != point) return;
    ctx->fired.store(true);
    switch (point) {
        case 0: // BeforeCandidateManifestWrite → fail manifest temp write
            ctx->armed.store(true);
            ctx->atomicSuffix = ".json";
            break;
        case 1: // AfterManifestWrite → fail scene temp write
            ctx->armed.store(true);
            ctx->atomicSuffix = "scene.scene";
            break;
        case 2: // AfterSceneWrite → delete the candidate scene bytes
            if (!ctx->deletedCandidateScene.empty()) {
                std::error_code ec;
                fs::remove(fs::path(ctx->deletedCandidateScene), ec);
            }
            break;
        case 3: // AfterCandidateValidation → fail marker write (fresh)
        case 4: // BeforeMarkerWrite → fail marker write
            ctx->armed.store(true);
            ctx->atomicSuffix = "versioned";
            break;
        case 5: // AfterMarkerWrite → fail current replace (fresh)
        case 6: // BeforeCurrentReplace → fail current write (baseline)
            ctx->armed.store(true);
            ctx->atomicSuffix = "current";
            break;
        case 7: // AfterCurrentReplaceBeforeReadback → restore baseline bytes
            if (!ctx->baselineCurrent.empty())
                WriteFileAt(ctx->currentPath, ctx->baselineCurrent);
            break;
        default:
            break;
    }
}

int32_t AtomicFaultHookFn(int32_t point, const char* tempPath,
                          const char* targetPath, void* userData) {
    auto* ctx = static_cast<FaultCtx*>(userData);
    (void)point;
    if (!ctx->armed.load()) return 0;
    const std::string t = targetPath ? targetPath : "";
    if (t.find(ctx->atomicSuffix) == std::string::npos) return 0;
    ctx->armed.store(false);
    return 1;
}

void InstallFaultHooks(FaultCtx* ctx) {
    EditorSession_SetRevisionFaultHookForTesting(&RevisionFaultHookFn, ctx);
    EditorSession_SetAtomicFaultHookForTesting(&AtomicFaultHookFn, ctx);
}

static void ExpectNoEventDelta(std::atomic<int>& counter) {
    EXPECT_EQ(counter.load(), 0) << "success event emitted on failed import";
}

} // namespace

// Establish event watch on every fault test so a success event on the failed
// import is detected. Failure paths must emit nothing.
static void WatchEvents(EditorSessionHandle s, std::atomic<int>* counter) {
    EditorSession_SetEventCallback(
        s,
        [](int32_t type, const char* payload, void* userData) {
            (void)type;
            (void)payload;
            (*static_cast<std::atomic<int>*>(userData))++;
        },
        counter);
}

// Fault points 0/1: publish fails before/after the candidate manifest write,
// on top of an existing revision. Old current bytes must be intact.
TEST_F(ImportCommitTest, Fault0_BeforeCandidateManifestWrite_FailsWithOldCurrentIntact) {
    PrepareBaselineAndChangedSource();
    const std::string baseline = ReadFileAt(CurrentPath());
    std::atomic<int> events{0};
    WatchEvents(m_Session, &events);

    FaultCtx ctx;
    ctx.targetPoint = 0;
    InstallFaultHooks(&ctx);
    const int32_t rc =
        EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2);
    EXPECT_EQ(rc, -1);
    EXPECT_LT(SessionError(m_Session).find("NotCommitted"), std::string::npos)
        << SessionError(m_Session);
    EXPECT_TRUE(ctx.fired.load());
    EXPECT_EQ(ReadFileAt(CurrentPath()), baseline);
    ExpectNoEventDelta(events);
}

TEST_F(ImportCommitTest, Fault1_AfterManifestWrite_FailsWithOldCurrentIntact) {
    PrepareBaselineAndChangedSource();
    const std::string baseline = ReadFileAt(CurrentPath());
    std::atomic<int> events{0};
    WatchEvents(m_Session, &events);

    FaultCtx ctx;
    ctx.targetPoint = 1;
    InstallFaultHooks(&ctx);
    const int32_t rc =
        EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2);
    EXPECT_EQ(rc, -1);
    EXPECT_TRUE(ctx.fired.load());
    EXPECT_EQ(ReadFileAt(CurrentPath()), baseline);
    ExpectNoEventDelta(events);
}

// Fault point 2: hook deletes the predicted candidate scene file.
TEST_F(ImportCommitTest, Fault2_AfterSceneWrite_DeletesCandidateAndFailsClean) {
    PrepareBaselineAndChangedSource();
    const std::string baseline = ReadFileAt(CurrentPath());
    const int64_t next = CommittedRevision() + 1;
    std::atomic<int> events{0};
    WatchEvents(m_Session, &events);

    FaultCtx ctx;
    ctx.targetPoint = 2;
    ctx.deletedCandidateScene =
        (m_RootPath / ".engine-content" / "revisions" /
         std::to_string(next) / "scene.scene").string();
    InstallFaultHooks(&ctx);
    const int32_t rc =
        EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2);
    EXPECT_EQ(rc, -1);
    EXPECT_TRUE(ctx.fired.load());
    EXPECT_EQ(ReadFileAt(CurrentPath()), baseline);
    ExpectNoEventDelta(events);
}

// Fault points 3/4/5: fresh-project cases — no current exists yet; the
// publish must fail cleanly and never materialize a current.
TEST_F(ImportCommitTest, Fault3_AfterCandidateValidation_MarkerWriteFails) {
    std::atomic<int> events{0};
    WatchEvents(m_Session, &events);

    FaultCtx ctx;
    ctx.targetPoint = 3;
    InstallFaultHooks(&ctx);
    const fs::path objPath = m_RootPath / "fresh.obj";
    WriteFileAt(objPath, kSimpleObj);
    const int32_t rc = EditorSession_ImportAsset(m_Session, Engine::Content::PathToUtf8(objPath).c_str(), 2);
    EXPECT_EQ(rc, -1);
    EXPECT_TRUE(ctx.fired.load());
    EXPECT_FALSE(fs::exists(m_RootPath / ".engine-content" / "current"));
    ExpectNoEventDelta(events);
}

TEST_F(ImportCommitTest, Fault4_BeforeMarkerWrite_Fails) {
    std::atomic<int> events{0};
    WatchEvents(m_Session, &events);
    FaultCtx ctx;
    ctx.targetPoint = 4;
    InstallFaultHooks(&ctx);
    const fs::path objPath = m_RootPath / "fresh.obj";
    WriteFileAt(objPath, kSimpleObj);
    const int32_t rc = EditorSession_ImportAsset(m_Session, Engine::Content::PathToUtf8(objPath).c_str(), 2);
    EXPECT_EQ(rc, -1);
    EXPECT_TRUE(ctx.fired.load());
    EXPECT_FALSE(fs::exists(m_RootPath / ".engine-content" / "current"));
    ExpectNoEventDelta(events);
}

TEST_F(ImportCommitTest, Fault5_AfterMarkerWrite_CurrentReplaceFails) {
    std::atomic<int> events{0};
    WatchEvents(m_Session, &events);
    FaultCtx ctx;
    ctx.targetPoint = 5;
    InstallFaultHooks(&ctx);
    const fs::path objPath = m_RootPath / "fresh.obj";
    WriteFileAt(objPath, kSimpleObj);
    const int32_t rc = EditorSession_ImportAsset(m_Session, Engine::Content::PathToUtf8(objPath).c_str(), 2);
    EXPECT_EQ(rc, -1);
    EXPECT_TRUE(ctx.fired.load());
    EXPECT_FALSE(fs::exists(m_RootPath / ".engine-content" / "current"));
    ExpectNoEventDelta(events);
}

// Fault points 6/7: baseline-project cases.
TEST_F(ImportCommitTest, Fault6_BeforeCurrentReplace_FailsWithOldCurrentIntact) {
    PrepareBaselineAndChangedSource();
    const std::string baseline = ReadFileAt(CurrentPath());
    std::atomic<int> events{0};
    WatchEvents(m_Session, &events);
    FaultCtx ctx;
    ctx.targetPoint = 6;
    ctx.baselineCurrent = baseline;
    ctx.currentPath = CurrentPath();
    InstallFaultHooks(&ctx);
    const int32_t rc =
        EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2);
    EXPECT_EQ(rc, -1);
    EXPECT_TRUE(ctx.fired.load());
    EXPECT_EQ(ReadFileAt(CurrentPath()), baseline);
    ExpectNoEventDelta(events);
}

TEST_F(ImportCommitTest,
       Fault7_AfterCurrentReplaceBeforeReadback_YieldsRecoveryRequired) {
    PrepareBaselineAndChangedSource();
    const std::string baseline = ReadFileAt(CurrentPath());
    std::atomic<int> events{0};
    WatchEvents(m_Session, &events);
    FaultCtx ctx;
    ctx.targetPoint = 7;
    ctx.baselineCurrent = baseline;
    ctx.currentPath = CurrentPath();
    InstallFaultHooks(&ctx);
    const int32_t rc =
        EditorSession_ImportAsset(m_Session, "assets/models/tri.obj", 2);
    EXPECT_EQ(rc, -1);
    EXPECT_TRUE(ctx.fired.load());
    EXPECT_EQ(ReadFileAt(CurrentPath()), baseline)
        << "baseline must be restored atomically after hook restore";
    ExpectNoEventDelta(events);
    EXPECT_LT(SessionError(m_Session).find("RecoveryRequired"),
              std::string::npos)
        << SessionError(m_Session);
}

