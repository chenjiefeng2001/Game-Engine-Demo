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

} // namespace
