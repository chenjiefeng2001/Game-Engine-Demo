/**
 * @file test_pick_transport.cpp
 * @brief HRC-3 D3 — pick request/result transport contract tests (ABI v1.3)
 *
 * 独立成文件的原因：这些用例会反复增删，若继续塞进 test_editor_capi.cpp
 * （2000+ 行的 P1/P2 综合证据），任何 block 移动都可能破坏既有 evidence。
 * 独立 target 也让本组用例的 abort 不会污染 test_bridge 的 44/44 信号。
 *
 * 本文件只承载 D3 transport contract，**不复制**任何 P1/P2 既有测试。
 */

#include <gtest/gtest.h>

#include "editor_bridge/capi.h"
// 与 test_bridge 同理：presentation readback seam 需要 EngineHost 内部类型，
// 刻意只取 C++ 证据，不新增 C ABI。
#include "EngineHost.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* kManifest = "assets/gp01/manifest.json";
constexpr const char* kScene = "assets/gp01/Main.scene";

/// 作用域内保证 host 被销毁。
/// 缺少它时，任何中途 ASSERT 失败都会泄漏 host；持有 persistent GL 上下文的
/// 泄漏 host 会让后续所有 `EngineHost_Create` 直接失败 —— 一次失败污染整组用例。
struct ScopedHost {
    EngineHostHandle handle = nullptr;
    RuntimeResult result{};

    bool Create() {
        handle = EngineHost_Create(&result);
        return handle != nullptr;
    }
    ~ScopedHost() {
        if (handle) EngineHost_Destroy(handle);
    }
};

int32_t Pump(EngineHostHandle handle, RuntimeResult* result) {
    return EngineHost_PumpOneFrame(handle, 0.016f, result);
}

/// 提交一次 pick 请求，返回 pending operation_id（>0），失败返回 -1。
int64_t RequestPick(EngineHostHandle handle, float x, float y,
                    RuntimeResult* result) {
    const float xy[2] = {x, y};
    const int32_t code = EngineHost_Dispatch(handle, ENGINE_HOST_OP_PICK, 0, 0,
                                            nullptr, nullptr, nullptr, xy,
                                            result);
    if (code <= 0) return -1;
    return static_cast<int64_t>(result->operation_id);
}

bool FetchPick(EngineHostHandle handle, PickResultPacket* out,
               RuntimeResult* result) {
    uint8_t buffer[ENGINE_HOST_PICK_RESULT_PACKET_BYTES] = {};
    const int32_t code = EngineHost_Fetch(handle, ENGINE_HOST_OP_PICK, 0,
                                         nullptr, nullptr, nullptr, buffer,
                                         sizeof(buffer), result);
    if (code != 0) return false;
    if (out) std::memcpy(out, buffer, sizeof(*out));
    return true;
}

/// 在 presentation target 内找到一个确实命中实体的坐标。
///
/// 刻意**不**用粗网格扫描：gp01 的 production frame 只有 7296/480000 个
/// 非背景像素（约 1.5%），网格几乎必然全部落空（早期版本正是这样失败的）。
/// 这里改为**从真实 color attachment 量出内容位置**，与 tests/test_pick 一致：
/// 已知坐标来自帧内容本身，而不是猜测或复制投影矩阵。
bool FindHitCoordinate(EngineHostHandle handle, float* x, float* y) {
    auto* host = static_cast<editor_bridge::EngineHost*>(handle);
    std::vector<uint8_t> pixels;
    if (host->ReadPresentationPixels(pixels) <= 0 || pixels.empty()) return false;

    const int width = host->PresentationTargetWidth();
    const int height = host->PresentationTargetHeight();
    if (static_cast<int>(pixels.size()) < width * height * 4) return false;

    const uint8_t clearR = 13, clearG = 13, clearB = 18;   // 0.05/0.05/0.07
    int bestX = -1, bestY = -1;
    for (int glY = height - 1; glY >= 0 && bestX < 0; --glY) {
        for (int glX = 0; glX < width; ++glX) {
            const size_t o = (static_cast<size_t>(glY) * width + glX) * 4;
            const int r = pixels[o], g = pixels[o + 1], b = pixels[o + 2];
            const bool isClear = std::abs(r - clearR) <= 2 &&
                                 std::abs(g - clearG) <= 2 &&
                                 std::abs(b - clearB) <= 2;
            const bool isBlack = r <= 2 && g <= 2 && b <= 2;
            if (isClear || isBlack) continue;
            bestX = glX;
            bestY = glY;
            break;
        }
    }
    if (bestX < 0) return false;

    // canonical 坐标为 top-left 原点（D3-C3）：py = H - 1 - y
    *x = static_cast<float>(bestX);
    *y = static_cast<float>(height - 1 - bestY);
    return true;
}

}  // namespace

// ============================================================================
// D3 判别实验：frame 2 的 pending-edit 执行本身是否会 abort？
//
// 这是**唯一已批准**的 D3 root-cause 实验。问题只有一个：
//   第二个 pump 里执行**任意** pending edit 就会 abort，还是只有 PICK 才会？
//
// 与此前失败的 D3PickRequestPumpFetchHappyPath 逐项相同，唯一差别是
// frame 2 入队的是**既有**的 GL op ASSIGN_SPRITE。
// ============================================================================
TEST(D3Transport, DiscriminatorAssignSpriteInSecondFrameDoesNotAbort) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    ScopedHost host;
    ASSERT_TRUE(host.Create()) << "EngineHost_Create failed";
    RuntimeResult& result = host.result;
    EngineHostHandle handle = host.handle;

    // ── Frame 1：普通 production frame ──
    ASSERT_EQ(EngineHost_OpenProject(handle, kManifest, kScene, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0) << "frame 1 must succeed";

    // ── Frame 2：入队**恰好一个**既有 pending edit（ASSIGN_SPRITE）──
    const int32_t code =
        EngineHost_Dispatch(handle, ENGINE_HOST_OP_ASSIGN_SPRITE, 0, 0,
                            nullptr, nullptr, nullptr, nullptr, &result);
    // 与 PICK 请求完全相同的受理语义
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_EDIT_ACCEPTED)
        << "ASSIGN_SPRITE must be accepted exactly as a PICK request was";
    EXPECT_GT(code, 0);
    EXPECT_GT(result.operation_id, 0u);

    // 关键判定：若此处 abort，说明问题**不在** PICK，而在既有 pending-edit
    // 执行路径（drain / frame-2 状态）。若通过，则故障是 PICK 专属。
    ASSERT_EQ(Pump(handle, &result), 0)
        << "VERDICT: a pre-existing pending edit also aborts in frame 2 -> "
           "D3 PICK is exonerated at this layer";
}

// ============================================================================
// 以下 5 个为本 session 中**曾经产生 RED 结果的原始测试**，逐字恢复。
// 覆盖 A → Pump → Fetch、capacity=1、Fetch-only、Stop 作废、generation 隔离。
// ============================================================================

TEST(EditorHostAbiTest, D3PickRequestPumpFetchHappyPath) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    ScopedHost host;
    ASSERT_TRUE(host.Create());
    RuntimeResult& result = host.result;
    EngineHostHandle handle = host.handle;
    ASSERT_EQ(EngineHost_OpenProject(handle, kManifest, kScene, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0);

    float hx = 0.0f, hy = 0.0f;
    ASSERT_TRUE(FindHitCoordinate(handle, &hx, &hy))
        << "no coordinate produced a Hit; scene content may be empty";

    const int64_t requestId = RequestPick(handle, hx, hy, &result);
    ASSERT_GT(requestId, 0);
    // 受理 != 已执行
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_EDIT_ACCEPTED);
    EXPECT_EQ(result.operation_id, static_cast<uint64_t>(requestId));

    // 请求**跨帧**：在服务它的那一帧之前不得有结果
    PickResultPacket early{};
    RuntimeResult earlyResult{};
    EXPECT_FALSE(FetchPick(handle, &early, &earlyResult))
        << "a pick result must not exist before the servicing frame";

    ASSERT_EQ(Pump(handle, &result), 0);

    PickResultPacket packet{};
    ASSERT_TRUE(FetchPick(handle, &packet, &result));
    // D3-C4：frame identity 必带；request_id == operation_id 是唯一配对
    EXPECT_EQ(packet.request_id, static_cast<uint64_t>(requestId));
    EXPECT_GT(packet.frame_id, 0u);
    EXPECT_EQ(packet.struct_size, 32u);
    EXPECT_EQ(packet.status, ENGINE_HOST_PICK_HIT);
    // D3-C2：有效 ID 恒 >= 1
    EXPECT_GE(packet.entity_id, 1u);
}

TEST(EditorHostAbiTest, D3PickSecondRequestIsBusyAndDoesNotReplaceOutstanding) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    ScopedHost host;
    ASSERT_TRUE(host.Create());
    RuntimeResult& result = host.result;
    EngineHostHandle handle = host.handle;
    ASSERT_EQ(EngineHost_OpenProject(handle, kManifest, kScene, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0);

    const int64_t idA = RequestPick(handle, 400.0f, 300.0f, &result);
    ASSERT_GT(idA, 0);

    // A 还在 outstanding（已受理、未取走）⇒ B 必须 BUSY
    const int64_t idB = RequestPick(handle, 401.0f, 301.0f, &result);
    EXPECT_LT(idB, 0);
    EXPECT_EQ(result.status, ENGINE_HOST_STATUS_PICK_BUSY)
        << "second request must be explicitly rejected as busy";
    EXPECT_EQ(result.stage, ENGINE_HOST_STAGE_EDIT)
        << "busy attribution must use the existing EDIT stage, not a new one";

    ASSERT_EQ(Pump(handle, &result), 0);
    PickResultPacket packet{};
    ASSERT_TRUE(FetchPick(handle, &packet, &result));
    // B 从未入队 ⇒ Fetch 返回的仍然是 A
    EXPECT_EQ(packet.request_id, static_cast<uint64_t>(idA));
}

TEST(EditorHostAbiTest, D3PickResultIsFetchOnlyAndStatusIsStructured) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    ScopedHost host;
    ASSERT_TRUE(host.Create());
    RuntimeResult& result = host.result;
    EngineHostHandle handle = host.handle;
    ASSERT_EQ(EngineHost_OpenProject(handle, kManifest, kScene, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0);

    // 越界：查询无效 ⇒ OUTSIDE_VIEWPORT（与 NO_HIT 结构化区分，且绝不 clamp）
    ASSERT_GT(RequestPick(handle, 99999.0f, 300.0f, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0);
    PickResultPacket oob{};
    ASSERT_TRUE(FetchPick(handle, &oob, &result));
    EXPECT_EQ(oob.status, ENGINE_HOST_PICK_OUTSIDE_VIEWPORT);
    EXPECT_EQ(oob.entity_id, 0u);

    // 非有限坐标：查询无效 ⇒ INVALID_COORDINATE
    ASSERT_GT(RequestPick(handle, std::numeric_limits<float>::quiet_NaN(),
                          300.0f, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0);
    PickResultPacket nanPacket{};
    ASSERT_TRUE(FetchPick(handle, &nanPacket, &result));
    EXPECT_EQ(nanPacket.status, ENGINE_HOST_PICK_INVALID_COORDINATE);

    // 背景（角落）⇒ NO_HIT：正常结果，**不是**失败
    ASSERT_GT(RequestPick(handle, 2.0f, 2.0f, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0);
    PickResultPacket bg{};
    ASSERT_TRUE(FetchPick(handle, &bg, &result));
    EXPECT_EQ(bg.status, ENGINE_HOST_PICK_NO_HIT);

    // Fetch 之后 outstanding 被消费：再取一次必须失败（而不是重复返回旧结果）
    PickResultPacket again{};
    RuntimeResult againResult{};
    EXPECT_FALSE(FetchPick(handle, &again, &againResult));
    EXPECT_EQ(againResult.status, ENGINE_HOST_STATUS_INVALID_STATE);
    EXPECT_EQ(againResult.stage, ENGINE_HOST_STAGE_EDIT);
}

TEST(EditorHostAbiTest, D3PickPendingResultInvalidatedByStop) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    ScopedHost host;
    ASSERT_TRUE(host.Create());
    RuntimeResult& result = host.result;
    EngineHostHandle handle = host.handle;
    ASSERT_EQ(EngineHost_OpenProject(handle, kManifest, kScene, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0);

    ASSERT_GT(RequestPick(handle, 400.0f, 300.0f, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0);

    // 产出但未取走 ⇒ outstanding
    ASSERT_EQ(EngineHost_Stop(handle, &result), 0);

    // teardown 后 Fetch：**不产生 packet**
    PickResultPacket stale{};
    RuntimeResult staleResult{};
    EXPECT_FALSE(FetchPick(handle, &stale, &staleResult));
    EXPECT_EQ(staleResult.status, ENGINE_HOST_STATUS_INVALID_STATE);
    EXPECT_EQ(staleResult.stage, ENGINE_HOST_STAGE_SHUTDOWN);
    // packet 缓冲必须保持未写入（失效是"没有结果"，不是"一个失败结果"）
    EXPECT_EQ(stale.struct_size, 0u);
}

TEST(EditorHostAbiTest, D3PickOldRequestCannotReappearAfterReopen) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    ScopedHost host;
    ASSERT_TRUE(host.Create());
    RuntimeResult& result = host.result;
    EngineHostHandle handle = host.handle;
    ASSERT_EQ(EngineHost_OpenProject(handle, kManifest, kScene, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0);

    const int64_t oldId = RequestPick(handle, 400.0f, 300.0f, &result);
    ASSERT_GT(oldId, 0);
    ASSERT_EQ(Pump(handle, &result), 0);

    ASSERT_EQ(EngineHost_CloseProject(handle, &result), 0);
    ASSERT_EQ(EngineHost_OpenProject(handle, kManifest, kScene, &result), 0);
    ASSERT_EQ(Pump(handle, &result), 0);

    // 旧 A 绝不能重新出现
    PickResultPacket resurrected{};
    RuntimeResult resurrectedResult{};
    EXPECT_FALSE(FetchPick(handle, &resurrected, &resurrectedResult))
        << "a pick result from before Close must not reappear after reopen";
    EXPECT_EQ(resurrected.struct_size, 0u);

    // 新一轮请求必须可用，且 request_id 与旧的**不同**
    const int64_t newId = RequestPick(handle, 400.0f, 300.0f, &result);
    ASSERT_GT(newId, 0);
    EXPECT_NE(newId, oldId);
    ASSERT_EQ(Pump(handle, &result), 0);
    PickResultPacket fresh{};
    ASSERT_TRUE(FetchPick(handle, &fresh, &result));
    EXPECT_EQ(fresh.request_id, static_cast<uint64_t>(newId));
}
