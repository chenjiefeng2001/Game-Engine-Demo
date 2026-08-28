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
    static constexpr const char* kScratchDir = "gp01_editor_scratch/gate_p2";

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
