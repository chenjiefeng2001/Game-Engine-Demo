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

// ── Phase 3-D (P3-D)：Runtime Play / Reload(_PERSIST) / Stop / 错误恢复 ──

/// 把一段 Lua 写入 scratch（适用 CWD 已在 scratch 根时）。
static void WriteScratchScript(const char* rel, const std::string& content) {
    std::filesystem::create_directories(
        std::filesystem::path(rel).parent_path());
    std::ofstream f(rel, std::ios::binary | std::ios::trunc);
    f << content;
}

/// 找（可选的）脚本资产索引：按 registry 序第一个 Script；未找到返回 -1。
static int FindScriptIndex(EditorSessionHandle s) {
    const int32_t n = EditorSession_GetAssetCount(s);
    for (int32_t i = 0; i < n; ++i)
        if (EditorSession_GetAssetType(s, i) == 1) return i;
    return -1;
}

TEST_F(EditorBridgeTest, RuntimePlayReloadPersistStop) {
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    // 导入一个 _PERSIST 探针脚本（D3b gold：初次 1 → Reload 后 2）
    const char* probeRel = "assets/gp01/scripts/probe.lua";
    WriteScratchScript(probeRel,
        "_PERSIST = _PERSIST or {}\n"
        "_PERSIST.n = (_PERSIST.n or 0) + 1\n"
        "function OnUpdate(dt) end\n");
    const int32_t probeIdx = EditorSession_ImportAsset(m_Session, probeRel, 1);
    ASSERT_GE(probeIdx, 0);

    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 0);
    // Play 前取 _PERSIST 探针：非运行态返回默认值
    EXPECT_EQ(EditorSession_RuntimePersistInt(m_Session, "n", 42), 42);

    EXPECT_EQ(EditorSession_Play(m_Session, probeIdx), 1);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 1);
    EXPECT_EQ(EditorSession_RuntimePersistInt(m_Session, "n", 0), 1)
        << "fresh play: _PERSIST.n must be 1";

    // Reload：保留 _PERSIST → n=2（S4）
    EXPECT_EQ(EditorSession_Reload(m_Session), 1);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 1);
    EXPECT_EQ(EditorSession_RuntimePersistInt(m_Session, "n", 0), 2)
        << "reload must preserve _PERSIST (1 -> 2)";

    // Stop → 回编辑态
    EditorSession_Stop(m_Session);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 0);

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, RuntimePlayDirectorAndTick) {
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    // assetIndex<0 = 项目 director（场景首个 scriptGuid → game.lua），应可跑
    EXPECT_EQ(EditorSession_Play(m_Session, -1), 1);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 1);
    // OnUpdate 单帧推进（无输入 provider → input.is_down=false，安全）
    EditorSession_RuntimeTick(m_Session, 0.016f);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 1);

    EditorSession_Stop(m_Session);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 0);
    // Reload 在非运行态被拒
    EXPECT_EQ(EditorSession_Reload(m_Session), 0);

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, RuntimeLuaErrorRecovery) {
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    const int32_t scriptIdx = FindScriptIndex(m_Session);
    ASSERT_GE(scriptIdx, 0);

    // ── 语法错误的脚本：Play 失败，Diagnostic 非空，Session 不崩 ──
    WriteScratchScript("assets/gp01/broken.lua", "function broken( end end end");
    const int32_t brokenIdx = EditorSession_ImportAsset(m_Session,
                                                        "assets/gp01/broken.lua", 1);
    ASSERT_GE(brokenIdx, 0);

    EXPECT_EQ(EditorSession_Play(m_Session, brokenIdx), 0);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 0);
    char err[512] = {};
    EXPECT_GT(EditorSession_GetRuntimeError(m_Session, err, sizeof(err)), 0);
    EXPECT_GT(std::strlen(err), 0u) << "Lua syntax error must be surfaced";

    // 修复后 Play 恢复
    WriteScratchScript("assets/gp01/broken.lua",
        "_PERSIST = _PERSIST or {}\nfunction OnUpdate(dt) end\n");
    EXPECT_EQ(EditorSession_Play(m_Session, brokenIdx), 1);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 1);

    // ── 运行中途把脚本改成语法错误 → Reload 失败（加载即 pcall 捕获），Session 存活 ──
    WriteScratchScript("assets/gp01/broken.lua",
        "function broke( end end end\n");
    EXPECT_EQ(EditorSession_Reload(m_Session), 0);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 1) << "session survives failed reload";
    EXPECT_GT(EditorSession_GetRuntimeError(m_Session, err, sizeof(err)), 0)
        << "Lua load error must be surfaced after failed reload";

    // 修复 → Reload 恢复
    WriteScratchScript("assets/gp01/broken.lua",
        "_PERSIST = _PERSIST or {}\nfunction OnUpdate(dt) end\n");
    EXPECT_EQ(EditorSession_Reload(m_Session), 1);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 1);

    EditorSession_Stop(m_Session);
    EXPECT_EQ(EditorSession_IsPlaying(m_Session), 0);

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, MissingGuidAssetWarnsAndEntityRemains) {
    // AV-G3 E3：场景引用 registry 不存在的 GUID → 契约内告警 + 实体保留 + Editor 可用。
    const std::string oldcwd = MakeScratchFacing();

    namespace fs = std::filesystem;
    // E3.scene：Player(有效 sprite) + GhostDangling(dangling script GUID)
    WriteScratchScript("assets/gp01/E3.scene",
        "{\n"
        "  \"format\": \"engine.scene\", \"version\": 1,\n"
        "  \"entities\": [\n"
        "    { \"name\": \"Player\", \"position\": [0,0,4], \"sprite\": \"d1000000000000000000000000000001\" },\n"
        "    { \"name\": \"GhostDangling\", \"position\": [1,0,2], \"script\": \"ffffffffffffffffffffffffffffffff\" }\n"
        "  ]\n"
        "}\n");

    // 打开成功（非致命），实体整批保留
    EXPECT_EQ(EditorSession_OpenProject(m_Session, kManifest, "assets/gp01/E3.scene"), 1);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 2);

    // 无脚本绑定（缺失资产的实体照常存在但不带该组件）
    int ghost = -1;
    for (int32_t i = 0; i < EditorSession_GetEntityCount(m_Session); ++i) {
        char name[64] = {};
        EditorSession_GetEntityName(m_Session, i, name, sizeof(name));
        if (std::strcmp(name, "GhostDangling") == 0) { ghost = i; break; }
    }
    ASSERT_GE(ghost, 0);
    char scr[256] = {};
    EXPECT_EQ(EditorSession_GetEntityScript(m_Session, ghost, scr, sizeof(scr)), 0);
    EXPECT_EQ(scr[0], '\0') << "dangling script GUID -> no binding, entity remains";

    // 告警可读（E3 观察通道）
    char warn[1024] = {};
    EXPECT_GT(EditorSession_GetWarnings(m_Session, warn, sizeof(warn)), 0);
    EXPECT_NE(std::strstr(warn, "missing"), nullptr);
    EXPECT_NE(std::strstr(warn, "GhostDangling"), nullptr);

    // Editor 仍可用（可保存回写 E3.scene）
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 1);
    EXPECT_TRUE(std::filesystem::exists("assets/gp01/E3.scene"));

    // 干净工程打开 → 无告警
    EXPECT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    warn[0] = '\0';
    EXPECT_EQ(EditorSession_GetWarnings(m_Session, warn, sizeof(warn)), 0);

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
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

// ════════════════════════════════════════════════════════════
// F1：Component Contract v1 — Camera 合同组件（生命周期/边界/落盘/重启）
// ════════════════════════════════════════════════════════════

static int FindEntityIndexByName(EditorSessionHandle s, const char* target) {
    const int32_t n = EditorSession_GetEntityCount(s);
    for (int32_t i = 0; i < n; ++i) {
        char name[128] = {};
        EditorSession_GetEntityName(s, i, name, sizeof(name));
        if (std::strcmp(name, target) == 0) return i;
    }
    return -1;
}

TEST_F(EditorBridgeTest, F1CameraComponentLifecycle) {
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    const int32_t idx = EditorSession_CreateEntity(m_Session, "CamTarget");
    ASSERT_GE(idx, 0);

    // 挂载 Camera：成功，类型为稳定串 "Camera"
    EXPECT_EQ(EditorSession_AddComponent(m_Session, idx, "Camera"), 0);
    EXPECT_EQ(EditorSession_HasComponent(m_Session, idx, "Camera"), 1);
    EXPECT_EQ(EditorSession_GetComponentCount(m_Session, idx), 1);
    char tn[64] = {};
    EXPECT_EQ(EditorSession_GetComponentTypeAt(m_Session, idx, 0, tn,
                                               sizeof(tn)),
              static_cast<int32_t>(std::strlen("Camera")));
    EXPECT_STREQ(tn, "Camera");

    // 重复 Add 幂等：仍是 1 份，不叠加
    EXPECT_EQ(EditorSession_AddComponent(m_Session, idx, "Camera"), 0);
    EXPECT_EQ(EditorSession_GetComponentCount(m_Session, idx), 1);

    // 反射属性读写（zoom / enabled / viewport / bounds）
    char v[256] = {};
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Camera",
                                                 "zoom", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("1")));
    EXPECT_STREQ(v, "1");          // 默认 zoom=1

    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "zoom", "2.5"), 0);
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Camera",
                                                 "zoom", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("2.5")));
    EXPECT_STREQ(v, "2.5");

    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "enabled", "false"), 0);
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Camera",
                                                 "enabled", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("false")));
    EXPECT_STREQ(v, "false");

    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "viewport", "0 0 800 600"), 0);
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Camera",
                                                 "viewport", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("0 0 800 600")));
    EXPECT_STREQ(v, "0 0 800 600");
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "bounds", "1 2 3 4"), 0);
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Camera",
                                                 "bounds", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("1 2 3 4")));
    EXPECT_STREQ(v, "1 2 3 4");

    EXPECT_EQ(EditorSession_IsDirty(m_Session), 1);

    // 移除：处理 -> 无，计数归零
    EXPECT_EQ(EditorSession_RemoveComponent(m_Session, idx, "Camera"), 0);
    EXPECT_EQ(EditorSession_HasComponent(m_Session, idx, "Camera"), 0);
    EXPECT_EQ(EditorSession_GetComponentCount(m_Session, idx), 0);

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, F1ComponentContractNegativePaths) {
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    const int32_t idx = EditorSession_CreateEntity(m_Session, "Neg");
    ASSERT_GE(idx, 0);

    // 未知契约类型拒绝（Add / Has / property 任一）
    EXPECT_LT(EditorSession_AddComponent(m_Session, idx, "Nope"), 0);
    EXPECT_EQ(EditorSession_HasComponent(m_Session, idx, "Nope"), 0);
    EXPECT_LT(EditorSession_RemoveComponent(m_Session, idx, "Nope"), 0);

    // 越界实体 / 空会话
    EXPECT_LT(EditorSession_AddComponent(m_Session, 99999, "Camera"), 0);
    EXPECT_LT(EditorSession_GetComponentProperty(m_Session, 99999, "Camera",
                                                 "zoom", nullptr, 0), 0);

    // 音符挂载前 SetComponentProperty 拒绝
    EXPECT_LT(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "zoom", "5"), 0);
    EXPECT_EQ(EditorSession_AddComponent(m_Session, idx, "Camera"), 0);

    // 未知属性 / 坏值 / 不可写 拒绝
    EXPECT_LT(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "nope", "1"), 0);
    EXPECT_LT(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "zoom", "abc"), 0);
    EXPECT_LT(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "enabled", "maybe"), 0);

    // 失败写不改值
    char v[64] = {};
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Camera",
                                                 "zoom", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("1")));
    EXPECT_STREQ(v, "1");

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, F1CameraSaveLoadColdRestart) {
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    const int32_t idx = EditorSession_CreateEntity(m_Session, "CameraRig");
    ASSERT_GE(idx, 0);

    EXPECT_EQ(EditorSession_AddComponent(m_Session, idx, "Camera"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "zoom", "3"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "enabled", "false"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "viewport", "0 0 800 600"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Camera",
                                                 "bounds", "1 2 3 4"), 0);
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 1);

    // 落盘证据：场景 JSON 含 components[] 且 Camera 类型的值
    {
        std::ifstream f("assets/gp01/Main.scene", std::ios::binary);
        ASSERT_TRUE(f.good());
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        EXPECT_NE(text.find("components"), std::string::npos)
            << "scene must now carry components[]";
        EXPECT_NE(text.find("Camera"), std::string::npos)
            << "scene must persist Camera component";
        EXPECT_NE(text.find("\"zoom\": 3"), std::string::npos);
    }

    // 冷重启等价：重新 OpenProject 走 LoadSnapshotFromFile→Instantiate 重建
    EXPECT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    const int32_t reloadIdx = FindEntityIndexByName(m_Session, "CameraRig");
    ASSERT_GE(reloadIdx, 0);
    EXPECT_EQ(EditorSession_HasComponent(m_Session, reloadIdx, "Camera"), 1);
    char v[256] = {};
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, reloadIdx, "Camera",
                                                 "zoom", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("3")));
    EXPECT_STREQ(v, "3");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, reloadIdx, "Camera",
                                                 "enabled", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("false")));
    EXPECT_STREQ(v, "false");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, reloadIdx, "Camera",
                                                 "viewport", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("0 0 800 600")));
    EXPECT_STREQ(v, "0 0 800 600");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, reloadIdx, "Camera",
                                                 "bounds", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("1 2 3 4")));
    EXPECT_STREQ(v, "1 2 3 4");

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, F1UnknownComponentTypeLoadWarnsAndEntityRemains) {
    const std::string oldcwd = MakeScratchFacing();
    // 场景引用一个未注册的契约组件类型 → 契约：告警 + 实体照常加载 + 无该组件
    WriteScratchScript("assets/gp01/F1Neg.scene",
        std::string("{\n")
        + "  \"format\": \"engine.scene\", \"version\": 1,\n"
        + "  \"entities\": [ { \"name\": \"Probe\", \"position\": [0,0,0],\n"
        + "     \"components\": [ { \"type\": \"NoSuchComponent\", \"data\": {} } ] } ]\n"
        + "}\n");
    EXPECT_EQ(EditorSession_OpenProject(m_Session, kManifest,
                                        "assets/gp01/F1Neg.scene"), 1);
    EXPECT_EQ(EditorSession_GetEntityCount(m_Session), 1);

    const int32_t probe = FindEntityIndexByName(m_Session, "Probe");
    ASSERT_GE(probe, 0);
    EXPECT_EQ(EditorSession_HasComponent(m_Session, probe, "NoSuchComponent"), 0);

    char warn[1024] = {};
    EXPECT_GT(EditorSession_GetWarnings(m_Session, warn, sizeof(warn)), 0);
    EXPECT_NE(std::strstr(warn, "unknown contract component type"), nullptr);
    EXPECT_NE(std::strstr(warn, "NoSuchComponent"), nullptr);

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

// ════════════════════════════════════════════════════════════
// F2：Collider Contract（F2-A 声明式上卷）—— 生命周期/边界/落盘/冷重启
//   不绑定 Physics（F2-B 才做）；验证多字段(Box) / 多类型(Bool/String/Float/Int)
//   反射与 components[] 组件化往返，证明 F1 模板非 Camera 特例。
// ════════════════════════════════════════════════════════════

TEST_F(EditorBridgeTest, F2ColliderContractLifecycle) {
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);

    const int32_t idx = EditorSession_CreateEntity(m_Session, "ColTarget");
    ASSERT_GE(idx, 0);

    // 挂载 Collider：稳定类型名 "Collider"（与 Camera 同源注册表）
    EXPECT_EQ(EditorSession_AddComponent(m_Session, idx, "Collider"), 0);
    EXPECT_EQ(EditorSession_HasComponent(m_Session, idx, "Collider"), 1);
    EXPECT_EQ(EditorSession_GetComponentCount(m_Session, idx), 1);
    char tn[64] = {};
    EXPECT_EQ(EditorSession_GetComponentTypeAt(m_Session, idx, 0, tn, sizeof(tn)),
              static_cast<int32_t>(std::strlen("Collider")));
    EXPECT_STREQ(tn, "Collider");

    // 重复 Add 幂等：仍是 1 份，不叠加（单实例语义）
    EXPECT_EQ(EditorSession_AddComponent(m_Session, idx, "Collider"), 0);
    EXPECT_EQ(EditorSession_GetComponentCount(m_Session, idx), 1);

    // 默认值
    char v[256] = {};
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Collider",
                                                 "shape", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("Circle")));
    EXPECT_STREQ(v, "Circle");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Collider",
                                                 "radius", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("0.5")));
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Collider",
                                                 "enabled", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("true")));

    // 多字段写读：切 Box + 半宽半高 + 传感器 + 碰撞配置（四类型全覆盖）
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "shape", "Box"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "halfX", "2.0"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "halfY", "1.5"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "isSensor", "true"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "category", "2"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "mask", "12"), 0);

    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Collider",
                                                 "shape", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("Box")));
    EXPECT_STREQ(v, "Box");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Collider",
                                                 "halfX", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("2")));
    EXPECT_STREQ(v, "2");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Collider",
                                                 "halfY", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("1.5")));
    EXPECT_STREQ(v, "1.5");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Collider",
                                                 "isSensor", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("true")));
    EXPECT_STREQ(v, "true");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Collider",
                                                 "category", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("2")));
    EXPECT_STREQ(v, "2");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Collider",
                                                 "mask", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("12")));
    EXPECT_STREQ(v, "12");

    EXPECT_EQ(EditorSession_IsDirty(m_Session), 1);

    // 移除：处理 -> 无，计数归零
    EXPECT_EQ(EditorSession_RemoveComponent(m_Session, idx, "Collider"), 0);
    EXPECT_EQ(EditorSession_HasComponent(m_Session, idx, "Collider"), 0);
    EXPECT_EQ(EditorSession_GetComponentCount(m_Session, idx), 0);

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, F2ColliderSaveLoadColdRestart) {
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    const int32_t idx = EditorSession_CreateEntity(m_Session, "ColRig");
    ASSERT_GE(idx, 0);

    EXPECT_EQ(EditorSession_AddComponent(m_Session, idx, "Collider"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "shape", "Box"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "halfX", "4.0"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "halfY", "3.0"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "isSensor", "true"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "category", "8"), 0);
    EXPECT_EQ(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "mask", "31"), 0);
    EXPECT_EQ(EditorSession_SaveProject(m_Session), 1);

    // 落盘证据：场景 JSON 含 components[] 中 Collider 及其参数
    {
        std::ifstream f("assets/gp01/Main.scene", std::ios::binary);
        ASSERT_TRUE(f.good());
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        EXPECT_NE(text.find("Collider"), std::string::npos)
            << "scene must persist Collider component";
        EXPECT_NE(text.find("\"shape\": \"Box\""), std::string::npos);
        EXPECT_NE(text.find("\"halfX\": 4"), std::string::npos);
        EXPECT_NE(text.find("\"category\": 8"), std::string::npos);
    }

    // 冷重启等价：重新 OpenProject → Deserialize → Attach 还原
    EXPECT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    const int32_t reloadIdx = FindEntityIndexByName(m_Session, "ColRig");
    ASSERT_GE(reloadIdx, 0);
    EXPECT_EQ(EditorSession_HasComponent(m_Session, reloadIdx, "Collider"), 1);
    char v[256] = {};
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, reloadIdx, "Collider",
                                                 "shape", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("Box")));
    EXPECT_STREQ(v, "Box");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, reloadIdx, "Collider",
                                                 "halfX", v, sizeof(v)), 1);
    EXPECT_STRCASEEQ(v, "4");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, reloadIdx, "Collider",
                                                 "halfY", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("3")));
    EXPECT_STRCASEEQ(v, "3");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, reloadIdx, "Collider",
                                                 "isSensor", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("true")));
    EXPECT_STREQ(v, "true");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, reloadIdx, "Collider",
                                                 "category", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("8")));
    EXPECT_STREQ(v, "8");
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, reloadIdx, "Collider",
                                                 "mask", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("31")));
    EXPECT_STREQ(v, "31");

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

TEST_F(EditorBridgeTest, F2ColliderNegativePaths) {
    const std::string oldcwd = MakeScratchFacing();
    ASSERT_EQ(EditorSession_OpenProject(m_Session, kManifest, kScene), 1);
    const int32_t idx = EditorSession_CreateEntity(m_Session, "ColNeg");
    ASSERT_GE(idx, 0);

    // 未知契约类型拒绝（Add）
    EXPECT_LT(EditorSession_AddComponent(m_Session, idx, "Nope"), 0);

    // 越界实体
    EXPECT_LT(EditorSession_AddComponent(m_Session, 99999, "Collider"), 0);

    // 挂载前 SetComponentProperty 拒绝
    EXPECT_LT(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "radius", "5"), 0);
    EXPECT_EQ(EditorSession_AddComponent(m_Session, idx, "Collider"), 0);

    // 未知属性 / 坏值拒绝（浮点、形状、布尔）
    EXPECT_LT(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "nope", "1"), 0);
    EXPECT_LT(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "radius", "abc"), 0);
    EXPECT_LT(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "shape", "Triangle"), 0);
    EXPECT_LT(EditorSession_SetComponentProperty(m_Session, idx, "Collider",
                                                 "isSensor", "maybe"), 0);

    // 失败写不改值（shape 仍为默认 Circle）
    char v[64] = {};
    EXPECT_EQ(EditorSession_GetComponentProperty(m_Session, idx, "Collider",
                                                 "shape", v, sizeof(v)),
              static_cast<int32_t>(std::strlen("Circle")));
    EXPECT_STREQ(v, "Circle");

    // 移除不存在的组件拒绝
    EXPECT_EQ(EditorSession_RemoveComponent(m_Session, idx, "Collider"), 0);
    EXPECT_LT(EditorSession_RemoveComponent(m_Session, idx, "Collider"), 0);

    (void)oldcwd;
    std::filesystem::current_path(oldcwd);
}

} // namespace
