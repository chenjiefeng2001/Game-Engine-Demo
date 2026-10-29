/**
 * @file ManifestStrictLoadTest.cpp
 * @brief Content v2 M1-B：Manifest 严格事务加载门禁（冻结路线 A）
 *
 * 覆盖：损坏 JSON / 缺字段 / 非法版本 / 未知类型 / 重复 GUID（含大小写与
 * 连字符变体）/ 重复 PathKey（分隔符与大小写）/ 结构违规路径 / 根外旧绝对
 * 路径的 K4.4 限界保留 / 失败不产生半状态。
 */

#include <gtest/gtest.h>
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/ProjectPaths.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Engine::Content::AssetType;
using Engine::Content::CaseSensitivity;
using Engine::Content::ContentRegistry;
using Engine::Content::ManifestLoadOptions;
using Engine::Content::PathError;
using Engine::Content::ProjectRoot;
using Engine::ResourceGUID;

namespace {

std::atomic<int> g_Unique{0};

constexpr const char* kGuidA = "a1000000000000000000000000000001";
constexpr const char* kGuidB = "b1000000000000000000000000000001";
constexpr const char* kGuidUpperA = "A1000000000000000000000000000001";
constexpr const char* kGuidDashedA = "a1000000-0000-0000-0000-000000000001";

nlohmann::json Entry(const std::string& guid, const std::string& path,
                     const std::string& type = "Texture") {
    return {{"guid", guid}, {"path", path}, {"type", type}};
}

nlohmann::json Manifest(const std::vector<nlohmann::json>& entries) {
    nlohmann::json j;
    j["version"] = 1;
    j["assets"] = nlohmann::json::array();
    for (const auto& e : entries) j["assets"].push_back(e);
    return j;
}

fs::path MakeTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("engine_m1b_manifest_" + std::to_string(stamp) + "_" +
            std::to_string(g_Unique.fetch_add(1)));
}

} // namespace

class ManifestStrictLoadTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_Dir = MakeTempDir();
        ASSERT_TRUE(fs::create_directories(m_Dir));
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_Dir, ec);
    }

    std::string WriteJson(const std::string& name,
                          const nlohmann::json& j) {
        const fs::path p = m_Dir / name;
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << j.dump(2);
        return p.generic_string();
    }

    std::string WriteRaw(const std::string& name, const std::string& text) {
        const fs::path p = m_Dir / name;
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << text;
        return p.generic_string();
    }

    ProjectRoot MakeRoot(const fs::path& dir) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        EXPECT_FALSE(ec) << ec.message();
        ProjectRoot root;
        PathError error = PathError::None;
        std::string diagnostic;
        EXPECT_TRUE(ProjectRoot::Create(dir.generic_string(), &root, &error,
                                        &diagnostic))
            << Engine::Content::ToString(error) << " " << diagnostic;
        return root;
    }

    void SeedRegistry(ContentRegistry* reg) {
        const ResourceGUID keep = reg->Import("keep.png", AssetType::Texture);
        ASSERT_FALSE(keep.IsNull());
        ASSERT_EQ(reg->Count(), 1u);
    }

    fs::path m_Dir;
};

// ── 合法清单：规范分隔符、GUID 变体、整体发布 ──────────────────

TEST_F(ManifestStrictLoadTest, AcceptsValidManifestAndCanonicalizesPaths) {
    auto j = Manifest({
        Entry(kGuidDashedA, "assets\\tex\\A.png"),
        Entry(kGuidB, "assets//scripts//b.lua", "Script"),
    });
    const std::string manifest = WriteJson("ok.json", j);

    ContentRegistry reg;
    ASSERT_TRUE(reg.LoadManifest(manifest));
    ASSERT_EQ(reg.Count(), 2u);

    const ResourceGUID a = ResourceGUID::FromHex(kGuidA);
    EXPECT_EQ(reg.ResolvePath(a), "assets/tex/A.png");
    EXPECT_TRUE(reg.ContainsPath("assets/tex/A.png"));
    EXPECT_EQ(reg.TypeOf(ResourceGUID::FromHex(kGuidB)), AssetType::Script);
}

// ── 损坏 / 截断 / 版本 / 字段 / 类型 ────────────────────────────

TEST_F(ManifestStrictLoadTest, CorruptJsonLeavesRegistryUnchanged) {
    ContentRegistry reg;
    SeedRegistry(&reg);
    const std::string manifest = WriteRaw("broken.json", "{ broken !!!");
    EXPECT_FALSE(reg.LoadManifest(manifest));

    // 截断：合法前缀在数组中途结束
    const std::string truncated = WriteRaw(
        "truncated.json",
        "{\"version\":1,\"assets\":[{\"guid\":\"a100000000000000000000000000"
        "0001\",\"path\":\"assets/a.png\",\"type\":\"Texture\"},{\"guid\":");
    EXPECT_FALSE(reg.LoadManifest(truncated));

    EXPECT_EQ(reg.Count(), 1u);
    EXPECT_TRUE(reg.ContainsPath("keep.png"));
}

TEST_F(ManifestStrictLoadTest, MissingOrInvalidVersionIsRejected) {
    ContentRegistry reg;
    SeedRegistry(&reg);

    auto missing = Manifest({Entry(kGuidA, "assets/a.png")});
    missing.erase("version");
    EXPECT_FALSE(reg.LoadManifest(WriteJson("v_missing.json", missing)));

    auto unsupported = Manifest({Entry(kGuidA, "assets/a.png")});
    unsupported["version"] = 2;
    EXPECT_FALSE(reg.LoadManifest(WriteJson("v2.json", unsupported)));

    auto wrongType = Manifest({Entry(kGuidA, "assets/a.png")});
    wrongType["version"] = "1";
    EXPECT_FALSE(reg.LoadManifest(WriteJson("v_string.json", wrongType)));

    EXPECT_EQ(reg.Count(), 1u);
}

TEST_F(ManifestStrictLoadTest, HalfEntryFailsWithoutPartialSwap) {
    ContentRegistry reg;
    SeedRegistry(&reg);

    auto missingType = Manifest({Entry(kGuidA, "assets/a.png"),
                                 {{"guid", kGuidB}, {"path", "assets/b.png"}}});
    EXPECT_FALSE(reg.LoadManifest(WriteJson("half.json", missingType)));
    EXPECT_EQ(reg.Count(), 1u);

    auto nonStringGuid = Manifest({Entry(kGuidA, "assets/a.png"),
                                   {{"guid", 42},
                                    {"path", "assets/b.png"},
                                    {"type", "Texture"}}});
    EXPECT_FALSE(reg.LoadManifest(WriteJson("bad_guid_type.json", nonStringGuid)));

    auto notObject = Manifest({Entry(kGuidA, "assets/a.png")});
    notObject["assets"].push_back(42);
    EXPECT_FALSE(reg.LoadManifest(WriteJson("not_object.json", notObject)));

    EXPECT_EQ(reg.Count(), 1u);
    EXPECT_TRUE(reg.ContainsPath("keep.png"));
}

TEST_F(ManifestStrictLoadTest, UnknownTypeIsRejected) {
    ContentRegistry reg;
    SeedRegistry(&reg);
    const auto manifest =
        Manifest({Entry(kGuidA, "assets/a.png", "Mesh")});
    EXPECT_FALSE(reg.LoadManifest(WriteJson("unknown_type.json", manifest)));
    EXPECT_EQ(reg.Count(), 1u);
}

// ── 重复 GUID：规范形式比较（大小写 / 连字符）──────────────────

TEST_F(ManifestStrictLoadTest, DuplicateGuidFailsIncludingCaseAndDashVariants) {
    ContentRegistry reg;

    EXPECT_FALSE(reg.LoadManifest(WriteJson(
        "dup_exact.json", Manifest({Entry(kGuidA, "assets/a.png"),
                                    Entry(kGuidA, "assets/b.png")}))));
    EXPECT_EQ(reg.Count(), 0u);

    EXPECT_FALSE(reg.LoadManifest(WriteJson(
        "dup_case.json", Manifest({Entry(kGuidA, "assets/a.png"),
                                   Entry(kGuidUpperA, "assets/b.png")}))));
    EXPECT_FALSE(reg.LoadManifest(WriteJson(
        "dup_dash.json", Manifest({Entry(kGuidA, "assets/a.png"),
                                   Entry(kGuidDashedA, "assets/b.png")}))));
    // 同 GUID 同路径重复也必须失败（清单重复条目不幂等）
    EXPECT_FALSE(reg.LoadManifest(WriteJson(
        "dup_same_path.json", Manifest({Entry(kGuidA, "assets/a.png"),
                                        Entry(kGuidA, "assets/a.png")}))));
    EXPECT_EQ(reg.Count(), 0u);
}

// ── 重复 PathKey：分隔符总是折叠；大小写按探测的卷能力 ─────────

TEST_F(ManifestStrictLoadTest, DuplicatePathKeyFollowsSeparatorsAndVolumeMode) {
    // 分隔符变体在任何卷能力下都必须是同一键
    ContentRegistry sepDup;
    EXPECT_FALSE(sepDup.LoadManifest(WriteJson(
        "sep_dup.json", Manifest({Entry(kGuidA, "assets/a.png"),
                                  Entry(kGuidB, "assets\\a.png")}))));
    EXPECT_EQ(sepDup.Count(), 0u);

    const std::string manifest = WriteJson(
        "case_alias.json", Manifest({Entry(kGuidA, "assets/A.png"),
                                     Entry(kGuidB, "assets/a.png")}));

    // 无根、显式大小写敏感：两个条目合法
    ContentRegistry sensitive;
    ManifestLoadOptions sensitiveOptions;
    sensitiveOptions.caseMode = CaseSensitivity::CaseSensitive;
    ASSERT_TRUE(sensitive.LoadManifest(manifest, sensitiveOptions));
    EXPECT_EQ(sensitive.Count(), 2u);

    // 有根：按探测到的卷能力判定
    const ProjectRoot root = MakeRoot(m_Dir / "Project");
    ASSERT_TRUE(root.IsValid());
    ContentRegistry byVolume;
    ManifestLoadOptions rootOptions;
    rootOptions.root = &root;
    if (root.CaseMode() == CaseSensitivity::CaseInsensitive) {
        EXPECT_FALSE(byVolume.LoadManifest(manifest, rootOptions));
        EXPECT_EQ(byVolume.Count(), 0u);
    } else {
        EXPECT_TRUE(byVolume.LoadManifest(manifest, rootOptions));
        EXPECT_EQ(byVolume.Count(), 2u);
    }
}

// ── 结构违规路径：不得降级为警告 ───────────────────────────────

TEST_F(ManifestStrictLoadTest, StructuralPathViolationsRejectWholeManifest) {
    const ProjectRoot root = MakeRoot(m_Dir / "Project");
    ASSERT_TRUE(root.IsValid());
    ManifestLoadOptions options;
    options.root = &root;
    options.allowLegacyAbsolute = true;

    const char* violations[] = {
        "../a.png", "a/../b.png", ".", "a/./b.png", "",
        "C:a.png", "//server/share/a.png", "\\\\?\\C:\\a.png",
        "\\\\.\\pipe\\asset",
    };
    int index = 0;
    for (const char* path : violations) {
        const std::string manifest = WriteJson(
            "bad_path_" + std::to_string(index++) + ".json",
            Manifest({Entry(kGuidA, path)}));
        ContentRegistry reg;
        EXPECT_FALSE(reg.LoadManifest(manifest, options)) << "path=" << path;
        EXPECT_EQ(reg.Count(), 0u) << "path=" << path;
    }
}

// ── K4.4 限界：根外旧绝对路径保留 + 警告；关闭开关则失败 ────────

TEST_F(ManifestStrictLoadTest, LegacyAbsoluteOutsideRootKeptOnlyWhenAllowed) {
    const ProjectRoot root = MakeRoot(m_Dir / "Project");
    ASSERT_TRUE(root.IsValid());
    const std::string outside =
        (m_Dir / "Outside" / "a.png").generic_string();
    const std::string manifest =
        WriteJson("legacy_abs.json", Manifest({Entry(kGuidA, outside)}));

    std::vector<std::string> warnings;
    ManifestLoadOptions legacy;
    legacy.root = &root;
    legacy.allowLegacyAbsolute = true;
    legacy.warnings = &warnings;

    ContentRegistry kept;
    ASSERT_TRUE(kept.LoadManifest(manifest, legacy));
    ASSERT_EQ(kept.Count(), 1u);
    EXPECT_EQ(kept.ResolvePath(ResourceGUID::FromHex(kGuidA)), outside);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(warnings[0].find("kept unresolved"), std::string::npos)
        << warnings[0];
    EXPECT_NE(warnings[0].find("EscapesRoot"), std::string::npos)
        << warnings[0];

    ManifestLoadOptions strict;
    strict.root = &root;
    ContentRegistry rejected;
    EXPECT_FALSE(rejected.LoadManifest(manifest, strict));
    EXPECT_EQ(rejected.Count(), 0u);
}

TEST_F(ManifestStrictLoadTest, AbsolutePathInsideRootCanonicalizes) {
    const ProjectRoot root = MakeRoot(m_Dir / "Project");
    ASSERT_TRUE(root.IsValid());
    const std::string inside =
        (m_Dir / "Project" / "assets" / "a.png").generic_string();
    const std::string manifest =
        WriteJson("inside_abs.json", Manifest({Entry(kGuidA, inside)}));

    ManifestLoadOptions options;
    options.root = &root;
    ContentRegistry reg;
    ASSERT_TRUE(reg.LoadManifest(manifest, options));
    EXPECT_EQ(reg.ResolvePath(ResourceGUID::FromHex(kGuidA)), "assets/a.png");
}

// ── K3/S09：缺失物理资产不等于身份丢失 ─────────────────────────

TEST_F(ManifestStrictLoadTest, MissingPhysicalAssetKeepsGuid) {
    // 加载器不检查物理存在性；缺失文件必须保留原 GUID 与路径，交给
    // Instantiate/UI 以告警形式处理（K3 身份保持）。
    const std::string manifest =
        WriteJson("missing_file.json",
                  Manifest({Entry(kGuidA, "assets/does_not_exist.png")}));
    ContentRegistry reg;
    ASSERT_TRUE(reg.LoadManifest(manifest));
    ASSERT_EQ(reg.Count(), 1u);
    EXPECT_EQ(reg.ResolvePath(ResourceGUID::FromHex(kGuidA)),
              "assets/does_not_exist.png");
}

// ── 失败不污染既有注册表（含“两好一坏”）────────────────────────

TEST_F(ManifestStrictLoadTest, FailureKeepsExistingEntriesIntact) {
    ContentRegistry reg;
    SeedRegistry(&reg);
    const std::string manifest = WriteJson(
        "two_good_one_bad.json",
        Manifest({Entry(kGuidA, "assets/a.png"),
                  Entry(kGuidB, "assets/b.png"),
                  Entry("not-a-guid", "assets/c.png")}));
    EXPECT_FALSE(reg.LoadManifest(manifest));
    EXPECT_EQ(reg.Count(), 1u);
    EXPECT_TRUE(reg.ContainsPath("keep.png"));
    EXPECT_FALSE(reg.ContainsPath("assets/a.png"));
    EXPECT_FALSE(reg.ContainsPath("assets/b.png"));
}
