/**
 * @file test_editor_capi.cpp
 * @brief EditorBridge C-ABI 契约测试（AV-005 Phase 0 Evidence Gate）
 *
 * 验证：句柄生命周期 / 工程打开 / 实体 CRUD / 事件回调 / 错误通道。
 * 走真实 DLL 导出（链接 EditorBridge），路径与 gate 相同的 GP01 工程。
 */

#include <gtest/gtest.h>
#include "editor_bridge/capi.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct CapturedEvent {
    int32_t type = 0;
    std::string payload;
};

static void CaptureCallback(int32_t type, const char* payload,
                            void* userData) {
    auto* log = static_cast<std::vector<CapturedEvent>*>(userData);
    log->push_back({type, payload ? payload : ""});
}

class EditorBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_Session = EditorSession_Create();
        ASSERT_NE(m_Session, nullptr);
    }
    void TearDown() override { EditorSession_Destroy(m_Session); }

    static constexpr const char* kManifest = "assets/gp01/manifest.json";
    static constexpr const char* kScene    = "assets/gp01/Main.scene";

    /// 写类用例的隔离 scratch。布局：<scratch>/assets/gp01/<GP01 内容>。
    /// 注意：MakeScratchFacing() 会把 CWD 切到 scratch 根，因此切完后
    /// 一律用 kManifest/kScene（相对路径），它们会精确落到副本上。
    // gate_p2 曾被 ASan 僵尸进程锁为 CWD（ledger §6 同类），换名避开；
    // 每个用例仍 remove_all + 重拷贝，保证隔离。
    static constexpr const char* kScratchDir = "gp01_editor_scratch/gate_p3";

    /// 复制 repo GP01 到 scratch（保留 assets/gp01 前缀）并切 CWD 到 scratch 根。
    /// 返回先前的 CWD 供还原。
    static std::string MakeScratchFacing();

    EditorSessionHandle m_Session = nullptr;
};

TEST_F(EditorBridgeTest, SessionLifecycle) {
    // 双重销毁安全性由调用方保证；此处验证创建即得非空、错误通道初始为空
    char buf[256] = {};
    EXPECT_EQ(EditorSession_GetLastError(m_Session, buf, sizeof(buf)), 0);
}

TEST_F(EditorBridgeTest, NullHandleContract) {
    EXPECT_EQ(EditorSession_GetEntityCount(nullptr), -1);
    EXPECT_EQ(EditorSession_GetAssetCount(nullptr), -1);
    EXPECT_EQ(EditorSession_CreateEntity(nullptr, "X"), -1);
    EXPECT_EQ(EditorSession_OpenProject(nullptr, "a", "b"), 0);
    EXPECT_NE(EditorSession_OpenProject(m_Session, nullptr, kScene), 1);
}

TEST_F(EditorBridgeTest, OpenProjectRoundTrip) {
    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureCallback, &events);

    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    const int32_t entities = EditorSession_GetEntityCount(m_Session);
    EXPECT_EQ(entities, 10);                       // GP01 基线：10 objects
    EXPECT_GE(EditorSession_GetAssetCount(m_Session), 33);   // 33 assets

    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events[0].type, EV_PROJECT_LOADED);
    EXPECT_NE(events[0].payload.find("objects=10"), std::string::npos);
}

TEST_F(EditorBridgeTest, CreateEntityAndEvents) {
    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureCallback, &events);
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    events.clear();

    const int32_t before = EditorSession_GetEntityCount(m_Session);
    const int32_t idx = EditorSession_CreateEntity(m_Session, "Probe");
    EXPECT_EQ(idx, before);                        // 追加到末尾
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), before + 1);

    char name[64] = {};
    EXPECT_EQ(EditorSession_GetEntityName(m_Session, idx, name, sizeof(name)),
              static_cast<int32_t>(strlen("Probe")));
    EXPECT_STREQ(name, "Probe");

    // 空名 → 自动命名 Entity_N（首个不跳号，GP-DX 语义）
    const int32_t idx2 = EditorSession_CreateEntity(m_Session, nullptr);
    char name2[64] = {};
    EditorSession_GetEntityName(m_Session, idx2, name2, sizeof(name2));
    EXPECT_STREQ(name2, "Entity");

    ASSERT_EQ(events.size(), static_cast<size_t>(2));
    EXPECT_EQ(events[0].type, EV_ENTITY_CREATED);
    EXPECT_EQ(events[0].payload, "Probe");
}

TEST_F(EditorBridgeTest, ErrorChannelOnBadPath) {
    char buf[256] = {};
    EXPECT_EQ(EditorSession_OpenProject(m_Session, "no/such.json",
                                        "no/such.scene"), 0);
    EditorSession_GetLastError(m_Session, buf, sizeof(buf));
    EXPECT_NE(std::strlen(buf), 0u);               // 失败必有可读原因

    // 失败后无场景：实体操作拒绝而非崩溃
    EXPECT_EQ(EditorSession_CreateEntity(m_Session, "Ghost"), -1);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), -1);
}

TEST_F(EditorBridgeTest, SaveProjectRequiresOpen) {
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 0);
    char buf[256] = {};
    EditorSession_GetLastError(m_Session, buf, sizeof(buf));
    EXPECT_NE(std::strstr(buf, "no project"), nullptr);
}

// ── Phase 1（P1-B/P1-C）：Transform 读写 + 资产查询 ──────────────

TEST_F(EditorBridgeTest, TransformRoundTrip) {
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureCallback, &events);

    float pos[3] = {};
    // Player 是场景内已知实体；先按名定位（GG1 契约路径）
    int playerIdx = -1;
    for (int32_t i = 0; i < EditorSession_GetEntityCount(m_Session); ++i) {
        char name[64] = {};
        EditorSession_GetEntityName(m_Session, i, name, sizeof(name));
        if (std::strcmp(name, "Player") == 0) { playerIdx = i; break; }
    }
    ASSERT_GE(playerIdx, 0);
    EXPECT_EQ(EditorSession_GetEntityPosition(m_Session, playerIdx, pos), 0);

    const float moved[3] = {pos[0], pos[1], pos[2] + 5.0f};
    EXPECT_EQ(EditorSession_SetEntityPosition(m_Session, playerIdx, moved), 0);

    float back[3] = {};
    EXPECT_EQ(EditorSession_GetEntityPosition(m_Session, playerIdx, back), 0);
    EXPECT_NEAR(back[2], moved[2], 1e-4f);   // 写后读一致

    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().type, EV_ENTITY_MOVED);
    EXPECT_NE(events.back().payload.find("idx="), std::string::npos);

    // 越界写拒绝
    const float junk[3] = {0, 0, 0};
    EXPECT_NE(EditorSession_SetEntityPosition(m_Session, 9999, junk), 0);
}

TEST_F(EditorBridgeTest, AssetQueryContract) {
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    const int32_t n = EditorSession_GetAssetCount(m_Session);
    ASSERT_GE(n, 33);

    int textures = 0, scripts = 0;
    for (int32_t i = 0; i < n; ++i) {
        char path[256] = {};
        EXPECT_GE(EditorSession_GetAssetPath(m_Session, i, path, sizeof(path)), 0);
        const int32_t t = EditorSession_GetAssetType(m_Session, i);
        ASSERT_TRUE(t == 0 || t == 1);
        if (t == 0) ++textures; else ++scripts;
    }
    EXPECT_GT(textures, 0);
    EXPECT_GT(scripts, 0);
    EXPECT_NE(EditorSession_GetAssetPath(m_Session, n + 5, nullptr, 0), 0);
}

// ── Phase 2 (AV-G2)：Delete / Rename / AssignSprite / Script / Dirty ──

TEST_F(EditorBridgeTest, DeleteEntityAndEvent) {
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureCallback, &events);
    events.clear();

    const int32_t before = EditorSession_GetEntityCount(m_Session);
    char gone[64] = {};
    EditorSession_GetEntityName(m_Session, 0, gone, sizeof(gone));

    EXPECT_EQ(EditorSession_DeleteEntity(m_Session, 0), 0);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), before - 1);

    // 越界 / 空会话拒绝
    EXPECT_NE(EditorSession_DeleteEntity(m_Session, 9999), 0);

    EXPECT_EQ(events[0].type, EV_ENTITY_DELETED);
    EXPECT_NE(events[0].payload.find("idx=0"), std::string::npos);
    EXPECT_NE(events[0].payload.find("name=" + std::string(gone)),
              std::string::npos);
}

TEST_F(EditorBridgeTest, RenameEntityRules) {
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureCallback, &events);
    events.clear();

    EXPECT_EQ(EditorSession_RenameEntity(m_Session, 0, "HeroRenamed"), 0);
    char name[64] = {};
    EditorSession_GetEntityName(m_Session, 0, name, sizeof(name));
    EXPECT_STREQ(name, "HeroRenamed");
    EXPECT_EQ(events[0].type, EV_ENTITY_RENAMED);
    EXPECT_NE(events[0].payload.find("new=HeroRenamed"), std::string::npos);

    // 空名拒绝；越界拒绝
    EXPECT_NE(EditorSession_RenameEntity(m_Session, 0, "   "), 0);
    EXPECT_NE(EditorSession_RenameEntity(m_Session, 9999, "X"), 0);
    char name2[64] = {};
    EditorSession_GetEntityName(m_Session, 0, name2, sizeof(name2));
    EXPECT_STREQ(name2, "HeroRenamed");   // 失败的写不生效
}

TEST_F(EditorBridgeTest, AssignSpriteTwiceIdempotentTexture) {
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    // 找第一个 Texture 资产
    const int32_t n = EditorSession_GetAssetCount(m_Session);
    int texIdx = -1;
    for (int32_t i = 0; i < n; ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 0) { texIdx = i; break; }
    ASSERT_GE(texIdx, 0);

    char spritePath[256] = {};
    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureCallback, &events);
    events.clear();

    EXPECT_EQ(EditorSession_AssignSprite(m_Session, texIdx, 0), 0);
    // 先取回再断言：EXPECT_EQ 两实参求值顺序未定义，不能在同表达式里
    // 用 strlen(spritePath) 与返回值比对。
    const int32_t spriteLen =
        EditorSession_GetEntitySprite(m_Session, 0, spritePath,
                                      sizeof(spritePath));
    EXPECT_EQ(spriteLen, static_cast<int32_t>(strlen(spritePath)));
    EXPECT_GT(std::strlen(spritePath), 0u);

    // 重复 assign 幂等：仍是同一路径，不再叠加组件
    EXPECT_EQ(EditorSession_AssignSprite(m_Session, texIdx, 0), 0);
    char again[256] = {};
    EditorSession_GetEntitySprite(m_Session, 0, again, sizeof(again));
    EXPECT_STREQ(spritePath, again);

    // 非 Texture 资产拒绝
    int scriptIdx = -1;
    for (int32_t i = 0; i < n; ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 1) { scriptIdx = i; break; }
    ASSERT_GE(scriptIdx, 0);
    EXPECT_NE(EditorSession_AssignSprite(m_Session, scriptIdx, 0), 0);

    EXPECT_EQ(events[0].type, EV_ENTITY_ASSIGNED);
}

TEST_F(EditorBridgeTest, ScriptBindingContract) {
    // P3-B：实体级 scriptGuid 绑定是序列化契约内数据（场景 JSON "script"），
    // 只写 binding 表 + dirty + 事件。持久化证据 = 落盘 JSON 含 script 字段
    // （同测试内 Destroy+Create 二次会话在 ASan/Windows 下会触发 GL 上下文
    // 重建死锁，ledger §6 僵尸模式 —— 重开一致性由 AV-G2 gate 覆盖）。
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    const int32_t n = EditorSession_GetAssetCount(m_Session);
    int scriptIdx = -1;
    for (int32_t i = 0; i < n; ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 1) { scriptIdx = i; break; }
    ASSERT_GE(scriptIdx, 0);
    int texIdx = -1;
    for (int32_t i = 0; i < n; ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 0) { texIdx = i; break; }
    ASSERT_GE(texIdx, 0);

    // GP01 场景里 Director(0) 已绑定 game.lua；选一个未绑定的实体验证 assign
    int target = -1;
    for (int32_t i = 0; i < EditorSession_GetEntityCount(m_Session); ++i) {
        char buf[256] = {};
        EditorSession_GetEntityScript(m_Session, i, buf, sizeof(buf));
        if (buf[0] == '\0') { target = i; break; }
    }
    ASSERT_GE(target, 0) << "expected an entity without script binding";

    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureCallback, &events);
    events.clear();

    char before[256] = {};
    EXPECT_EQ(EditorSession_GetEntityScript(m_Session, target, before, sizeof(before)), 0);

    EXPECT_EQ(EditorSession_AssignScript(m_Session, scriptIdx, target), 0);
    char after[256] = {};
    EXPECT_GT(EditorSession_GetEntityScript(m_Session, target, after, sizeof(after)), 0);
    EXPECT_NE(after[0], '\0');
    EXPECT_EQ(events[0].type, EV_ENTITY_SCRIPT_ASSIGNED);

    // 非 Script 资产拒绝（Texture 不能 Assign 为脚本）
    EXPECT_NE(EditorSession_AssignScript(m_Session, texIdx, target), 0);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 1);

    // Save → 读回场景 JSON：脚本 GUID 出现次数从 1（Director）变为 2
    // （目标实体也带 script 字段 = 真实落盘证据）
    char scriptGuidHex[64] = {};
    ASSERT_GE(EditorSession_GetAssetGuid(m_Session, scriptIdx, scriptGuidHex,
                                         sizeof(scriptGuidHex)), 0);
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 1);
    {
        std::ifstream f("assets/gp01/Main.scene", std::ios::binary);
        ASSERT_TRUE(f.good()) << "saved scene readable";
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        const std::string guid(scriptGuidHex);
        size_t pos = 0, count = 0;
        while ((pos = text.find(guid, pos)) != std::string::npos) { ++count; pos += guid.size(); }
        EXPECT_GE(count, 2u)
            << "script GUID must appear >=2 times (Director + assigned entity), got "
            << count;
    }

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, ScriptReadWriteViaRegistry) {
    const std::string oldcwd = MakeScratchFacing();
    // CWD 已在 scratch 根：相对路径落到副本，写操作不污染 repo 资产
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    const int32_t n = EditorSession_GetAssetCount(m_Session);
    int scriptIdx = -1;
    for (int32_t i = 0; i < n; ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 1) { scriptIdx = i; break; }
    ASSERT_GE(scriptIdx, 0);

    // game.lua 现为 ~12.6KB：缓冲必须大于文件，否则按 cap-1 截断
    char original[16384] = {};
    ASSERT_GE(EditorSession_ScriptRead(m_Session, scriptIdx, original,
                                       sizeof(original)), 0);
    EXPECT_GT(std::strlen(original), 100u);   // 真实 game.lua 非空

    // 追加一行注释并写回，再读回验证
    std::string edited = std::string(original) + "\n-- audit: AV-G2 script edit\n";
    ASSERT_LT(edited.size(), sizeof(original) - 1);   // 缓冲足够容纳改写
    EXPECT_EQ(EditorSession_ScriptSave(m_Session, scriptIdx, edited.c_str()), 0);
    char reread[16384] = {};
    EXPECT_EQ(EditorSession_ScriptRead(m_Session, scriptIdx, reread,
                                       sizeof(reread)),
              static_cast<int32_t>(edited.size()));
    EXPECT_STREQ(reread, edited.c_str());

    // 非脚本资产拒绝
    int texIdx = -1;
    for (int32_t i = 0; i < n; ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 0) { texIdx = i; break; }
    ASSERT_GE(texIdx, 0);
    EXPECT_LT(EditorSession_ScriptRead(m_Session, texIdx, original,
                                       sizeof(original)), 0);

    // 本轮改写在 scratch 副本上可丢弃；还原 CWD 而不必还原原文
    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, ImportAssetContract) {
    // P3-C：Import 幂等 + 事件 + 持久化（SaveManifest 落盘 GUID 稳定）。
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    const int32_t before = EditorSession_GetAssetCount(m_Session);
    ASSERT_GT(before, 0);

    // 复制一个已有纹理到 scratch 的新路径作为导入源（文件必须真实存在）
    namespace fs = std::filesystem;
    const char* importPath = "assets/gp01/tex/imported_tex.png";
    fs::copy("assets/gp01/tex/pad.png", importPath,
             fs::copy_options::overwrite_existing);

    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureCallback, &events);
    events.clear();

    int32_t idx = EditorSession_ImportAsset(m_Session, importPath, 0);
    ASSERT_GE(idx, 0) << "import texture should succeed";
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), before + 1);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 1);   // Import -> Dirty
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].type, EV_ASSET_IMPORTED);

    char guid[64] = {};
    ASSERT_GE(EditorSession_GetAssetGuid(m_Session, idx, guid, sizeof(guid)), 0);
    EXPECT_GT(std::strlen(guid), 0u);

    // 幂等：同路径再次导入返回同一资产索引，数量不涨
    int32_t again = EditorSession_ImportAsset(m_Session, importPath, 0);
    EXPECT_EQ(again, idx);
    EXPECT_EQ(EditorSession_GetAssetCount(m_Session), before + 1);

    // 文件不存在 → 拒绝（不登记、不假成功）
    EXPECT_LT(EditorSession_ImportAsset(m_Session,
                                        "assets/gp01/tex/no_such_file.png", 0), 0);
    // 非法类型拒绝
    EXPECT_LT(EditorSession_ImportAsset(m_Session, importPath, 42), 0);

    // 持久化证据：Save → 读回 manifest，GUID 出现
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 1);
    {
        std::ifstream f("assets/gp01/manifest.json", std::ios::binary);
        ASSERT_TRUE(f.good());
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        EXPECT_NE(text.find(guid), std::string::npos)
            << "imported GUID must persist to manifest";
        EXPECT_NE(text.find(importPath), std::string::npos)
            << "imported path must persist to manifest";
    }

    (void)oldcwd;
    fs::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, AssetIndexStableAcrossImports) {
    // P3-C 契约：资产 index 在会话内必须稳定。ContentRegistry::GetAllEntries()
    // 迭代 unordered_map，二次导入触发重排会让旧 index 指向别的资产
    // （实证：导入纹理后导入脚本，rename 落到了 game.lua）。bridge 层按 GUID
    // 排序兜底 → 新增/改名不位移既有索引。
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    namespace fs = std::filesystem;
    const char* pathA = "assets/gp01/tex/stable_a.png";
    fs::copy("assets/gp01/tex/pad.png", pathA,
             fs::copy_options::overwrite_existing);
    const int32_t idxA = EditorSession_ImportAsset(m_Session, pathA, 0);
    ASSERT_GE(idxA, 0);

    char guidA[64] = {};
    ASSERT_GE(EditorSession_GetAssetGuid(m_Session, idxA, guidA, sizeof(guidA)), 0);
    EXPECT_GT(std::strlen(guidA), 0u);

    // 导入第二个资产（触发 registry 重排的路径）
    const char* pathB = "assets/gp01/tex/stable_b.png";
    fs::copy("assets/gp01/tex/pad.png", pathB,
             fs::copy_options::overwrite_existing);
    ASSERT_GE(EditorSession_ImportAsset(m_Session, pathB, 0), 0);

    // idxA 仍必须指向同一个资产：GUID 不变 + path 不变
    char guidA2[64] = {};
    ASSERT_GE(EditorSession_GetAssetGuid(m_Session, idxA, guidA2, sizeof(guidA2)), 0);
    EXPECT_STREQ(guidA, guidA2)
        << "asset index must be stable across imports (was: " << guidA2 << ")";
    char pathAtIdx[256] = {};
    ASSERT_GE(EditorSession_GetAssetPath(m_Session, idxA, pathAtIdx, sizeof(pathAtIdx)), 0);
    EXPECT_STREQ(pathAtIdx, pathA)
        << "index must still resolve to the same asset";

    (void)oldcwd;
    fs::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, RenameAssetKeepsGuidAndBinding) {
    // P3-C：Rename 改路径不改 GUID（DL-02：绑定稳定）。
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    // 找一个脚本资产做 rename 目标（game.lua）
    const int32_t n = EditorSession_GetAssetCount(m_Session);
    int scriptIdx = -1;
    for (int32_t i = 0; i < n; ++i)
        if (EditorSession_GetAssetType(m_Session, i) == 1) { scriptIdx = i; break; }
    ASSERT_GE(scriptIdx, 0);

    char oldPath[512] = {};
    ASSERT_GE(EditorSession_GetAssetPath(m_Session, scriptIdx, oldPath,
                                         sizeof(oldPath)), 0);
    char oldGuid[64] = {};
    ASSERT_GE(EditorSession_GetAssetGuid(m_Session, scriptIdx, oldGuid,
                                         sizeof(oldGuid)), 0);

    // 空名/带路径分隔符拒绝
    EXPECT_NE(EditorSession_RenameAsset(m_Session, scriptIdx, ""), 0);
    EXPECT_NE(EditorSession_RenameAsset(m_Session, scriptIdx, "a/b"), 0);
    EXPECT_NE(EditorSession_RenameAsset(m_Session, 99999, "x"), 0);

    std::vector<CapturedEvent> events;
    EditorSession_SetEventCallback(m_Session, &CaptureCallback, &events);
    events.clear();

    EXPECT_EQ(EditorSession_RenameAsset(m_Session, scriptIdx, "game_v2"), 0);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 1);
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].type, EV_ASSET_RENAMED);

    // GUID 不变，路径变为 game_v2.lua（扩展名按原文件补全）
    char newGuid[64] = {};
    ASSERT_GE(EditorSession_GetAssetGuid(m_Session, scriptIdx, newGuid,
                                         sizeof(newGuid)), 0);
    EXPECT_STREQ(oldGuid, newGuid) << "rename must keep GUID (binding stable)";
    char newPath[512] = {};
    ASSERT_GE(EditorSession_GetAssetPath(m_Session, scriptIdx, newPath,
                                         sizeof(newPath)), 0);
    EXPECT_NE(std::string(newPath).find("game_v2.lua"), std::string::npos)
        << "path must reflect new basename, got: " << newPath;

    // 物理文件确实改名（Save 后读回可验证）
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 1);
    {
        std::ifstream f("assets/gp01/manifest.json", std::ios::binary);
        ASSERT_TRUE(f.good());
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        EXPECT_NE(text.find("game_v2.lua"), std::string::npos);
        EXPECT_EQ(text.find("game.lua"), std::string::npos)
            << "old path should be gone from manifest after rename";
    }
    EXPECT_TRUE(std::filesystem::exists("assets/gp01/game_v2.lua"));
    EXPECT_FALSE(std::filesystem::exists("assets/gp01/game.lua"));

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, DirtyStateContract) {
    const std::string oldcwd = MakeScratchFacing();
    // CWD 已在 scratch 根：SaveProject 会真实写盘，落在副本上
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 0);   // 打开即 Clean

    float pos[3] = {};
    int playerIdx = -1;
    for (int32_t i = 0; i < EditorSession_GetEntityCount(m_Session); ++i) {
        char name[64] = {};
        EditorSession_GetEntityName(m_Session, i, name, sizeof(name));
        if (std::strcmp(name, "Player") == 0) { playerIdx = i; break; }
    }
    ASSERT_GE(playerIdx, 0);
    EditorSession_GetEntityPosition(m_Session, playerIdx, pos);
    const float moved[3] = {pos[0], pos[1], pos[2] + 1.0f};
    EXPECT_EQ(EditorSession_SetEntityPosition(m_Session, playerIdx, moved), 0);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 1);   // Edit -> Dirty

    // Save 会写回磁盘（因此这里必须真实保存再恢复，保证后续用例基线干净）
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 1);
    EXPECT_EQ(EditorSession_IsDirty(m_Session), 0);   // Save -> Clean
}

std::string EditorBridgeTest::MakeScratchFacing() {
    namespace fs = std::filesystem;
    fs::remove_all(kScratchDir);
    // 目标：<scratch>/assets/gp01/<内容>
    const fs::path dst = fs::path(kScratchDir) / "assets" / "gp01";
    fs::create_directories(dst);
    fs::copy("assets/gp01", dst,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    const std::string old = fs::current_path().string();
    fs::current_path(kScratchDir);   // 让 assets/gp01/... 落点到副本
    return old;
}

} // namespace
