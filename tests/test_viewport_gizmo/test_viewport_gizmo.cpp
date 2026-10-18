// Phase 7-C — Gizmo ABI ↔ ViewportPanel state
//
// 目标：锁住 Phase 7-C 修的那条断链，并防止它以静默错位的形式回来。
//
// 背景（docs/Avalonia-Phase7-Viewport-Charter.md §3.4）：9 个 Gizmo ABI 早已声明并实现，
// 但实现只写 EditorSession 自己的成员，既不触达 ViewportPanel，也没有消费者。
// 现在它们在有 Application 时会真正转发给 ViewportPanel。
//
// 本 target 覆盖三层，各自都能在无 GL、无窗口的环境下跑：
//   1. ABI 4 值协议 → ViewportTool 5 值枚举的映射（最容易静默错位的一层）
//   2. ViewportPanel 的 gizmo 状态 setter/getter 往返
//   3. 经 C ABI 的往返，走「没有 Application」这条回退路径
//
// 刻意**不**构造 Application 来验证转发：那需要 OpenGLGraphicsFactory + headless
// 初始化，成本远高于本组用例能证明的东西。转发本身由 test_bridge 的既有
// EngineHost 用例间接覆盖。

#include <gtest/gtest.h>

#include <cstdint>

extern "C" {
#include "editor_bridge/capi.h"
}

#include "Engine/Editor/ViewportPanel.h"

namespace {

using Engine::ViewportPanel;
using Engine::ViewportTool;

constexpr float kTol = 1e-6f;

// ────────────────────────────────────────────────────────────────────
// 1. ABI 协议 ↔ ViewportTool 枚举
//
// ABI 是 4 值协议（0=Select, 1=Translate, 2=Rotate, 3=Scale，见 capi.h:308），
// 而 ViewportTool 有 5 个值且多一个 Hand：
//     Select=0, Hand=1, Translate=2, Rotate=3, Scale=4
// 两者**下标不对齐**。直接 static_cast 会把 Translate 变成 Hand、
// 把 Scale 变成 Rotate —— 不崩溃，只是工具栏和实际 gizmo 永远差一档。
// ────────────────────────────────────────────────────────────────────

TEST(GizmoAbiMode, MapsTheFourProtocolValuesToTheRightTools) {
    ViewportTool tool = ViewportTool::Select;

    ASSERT_TRUE(ViewportPanel::TryGizmoModeFromAbi(0, tool));
    EXPECT_EQ(tool, ViewportTool::Select);

    ASSERT_TRUE(ViewportPanel::TryGizmoModeFromAbi(1, tool));
    EXPECT_EQ(tool, ViewportTool::Translate);

    ASSERT_TRUE(ViewportPanel::TryGizmoModeFromAbi(2, tool));
    EXPECT_EQ(tool, ViewportTool::Rotate);

    ASSERT_TRUE(ViewportPanel::TryGizmoModeFromAbi(3, tool));
    EXPECT_EQ(tool, ViewportTool::Scale);
}

// 协议外值必须被拒绝，而不是被当成某个合法工具。
TEST(GizmoAbiMode, RejectsValuesOutsideTheProtocol) {
    ViewportTool tool = ViewportTool::Rotate;   // 预置为非默认值以确认未被写入
    EXPECT_FALSE(ViewportPanel::TryGizmoModeFromAbi(4, tool));
    EXPECT_EQ(tool, ViewportTool::Rotate);
    EXPECT_FALSE(ViewportPanel::TryGizmoModeFromAbi(-1, tool));
    EXPECT_EQ(tool, ViewportTool::Rotate);
}

TEST(GizmoAbiMode, RoundTripsEveryProtocolValue) {
    for (int32_t abi = 0; abi <= 3; ++abi) {
        ViewportTool tool = ViewportTool::Select;
        ASSERT_TRUE(ViewportPanel::TryGizmoModeFromAbi(abi, tool));
        EXPECT_EQ(ViewportPanel::GizmoModeToAbi(tool), abi);
    }
}

// Hand 在 ABI 协议里没有对应值，映射到 Select 而不是静默落到 Translate。
TEST(GizmoAbiMode, HandMapsToSelectBecauseTheProtocolHasNoHand) {
    EXPECT_EQ(ViewportPanel::GizmoModeToAbi(ViewportTool::Hand), 0);
}

// ────────────────────────────────────────────────────────────────────
// 2. ViewportPanel gizmo 状态
// ────────────────────────────────────────────────────────────────────

// 这些 setter 此前不存在，m_GizmoLocal / m_SnapEnabled / m_SnapValue
// 只能由本类 ImGui 浮层按钮写入，外部无法触达。
TEST(ViewportPanelGizmoState, RoundTripsSpaceAndSnap) {
    ViewportPanel vp("gizmo-test");

    EXPECT_FALSE(vp.GetGizmoSpace());
    EXPECT_FALSE(vp.GetGizmoSnapEnabled());
    EXPECT_FLOAT_EQ(vp.GetGizmoSnapValue(), 0.5f);

    vp.SetGizmoSpace(true);
    EXPECT_TRUE(vp.GetGizmoSpace());
    vp.SetGizmoSpace(false);
    EXPECT_FALSE(vp.GetGizmoSpace());

    vp.SetGizmoSnapEnabled(true);
    EXPECT_TRUE(vp.GetGizmoSnapEnabled());

    vp.SetGizmoSnapValue(0.25f);
    EXPECT_FLOAT_EQ(vp.GetGizmoSnapValue(), 0.25f);
    vp.SetGizmoSnapValue(45.0f);
    EXPECT_FLOAT_EQ(vp.GetGizmoSnapValue(), 45.0f);
}

TEST(ViewportPanelGizmoState, ToolRoundTripsThroughTheAbiMapping) {
    ViewportPanel vp("gizmo-test");
    vp.SetTool(ViewportTool::Rotate);
    EXPECT_EQ(vp.GetTool(), ViewportTool::Rotate);
    EXPECT_EQ(ViewportPanel::GizmoModeToAbi(vp.GetTool()), 2);
}

// ────────────────────────────────────────────────────────────────────
// 3. 经 C ABI 的往返（无 Application 的回退路径）
//
// EditorSession_Create() 走默认构造，m_Application 为 nullptr，因此这些用例
// 走的是「无 Application 时退回成员缓存」那条分支 —— 也就是修断链之前
// 唯一实际生效的行为。锁定它是为了保证修复没有顺手改掉 legacy 语义。
// ────────────────────────────────────────────────────────────────────

class GizmoAbiFallbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        // EditorSession_Create 在未注册主线程 / 未注册 GLFW owner 时直接返回
        // nullptr（capi.cpp:187-201），不是构造失败。
        ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
        session_ = EditorSession_Create();
        ASSERT_NE(session_, nullptr);
    }
    void TearDown() override {
        if (session_) EditorSession_Destroy(session_);
        session_ = nullptr;
    }
    EditorSessionHandle session_ = nullptr;
};

TEST_F(GizmoAbiFallbackTest, GizmoStateRoundTripsWithoutApplication) {
    EditorSession_SetGizmoMode(session_, 2);
    EXPECT_EQ(EditorSession_GetGizmoMode(session_), 2);

    EditorSession_SetGizmoSpace(session_, 1);
    EXPECT_EQ(EditorSession_GetGizmoSpace(session_), 1);
    EditorSession_SetGizmoSpace(session_, 0);
    EXPECT_EQ(EditorSession_GetGizmoSpace(session_), 0);

    EditorSession_SetGizmoSnap(session_, 1, 0.25f);
    EXPECT_EQ(EditorSession_GetGizmoSnapEnabled(session_), 1);
    EXPECT_NEAR(EditorSession_GetGizmoSnapValue(session_), 0.25f, kTol);

    EditorSession_SetGizmoSnap(session_, 0, 1.0f);
    EXPECT_EQ(EditorSession_GetGizmoSnapEnabled(session_), 0);
    EXPECT_NEAR(EditorSession_GetGizmoSnapValue(session_), 1.0f, kTol);
}

TEST_F(GizmoAbiFallbackTest, GridVisibilityRoundTripsWithoutApplication) {
    EditorSession_SetViewportGridVisible(session_, 0);
    EXPECT_EQ(EditorSession_GetViewportGridVisible(session_), 0);
    EditorSession_SetViewportGridVisible(session_, 1);
    EXPECT_EQ(EditorSession_GetViewportGridVisible(session_), 1);
}

// 越界的 gizmo mode 不得悄悄改成某个合法工具：缓存保留调用方的原值。
TEST_F(GizmoAbiFallbackTest, OutOfRangeGizmoModeIsRememberedVerbatim) {
    EditorSession_SetGizmoMode(session_, 99);
    EXPECT_EQ(EditorSession_GetGizmoMode(session_), 99);
    EditorSession_SetGizmoMode(session_, -3);
    EXPECT_EQ(EditorSession_GetGizmoMode(session_), -3);
}

// 协议外的 space 值同样按 int 原样存取（0/1 之外的语义未定义，不做猜测）。
TEST_F(GizmoAbiFallbackTest, GizmoSpaceIsStoredAsGiven) {
    EditorSession_SetGizmoSpace(session_, 7);
    EXPECT_EQ(EditorSession_GetGizmoSpace(session_), 7);
}

}  // namespace