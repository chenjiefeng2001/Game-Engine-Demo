// ═══════════════════════════════════════════════════════════════════════════
// production presentation target 的尺寸策略测试
// ═══════════════════════════════════════════════════════════════════════════
//
// 这一层是纯逻辑，不碰 GL —— 正因如此它可以在任何机器上穷举，而 FBO 重建本身
// 实测在本地环境不可靠（跨线程 GL name 析构会污染后续 GLFW 初始化）。
//
// 覆盖的是 P1 需求里最容易回归、且旧代码从未真正走过的几条：去重、零尺寸、
// 恢复、越界，以及"尺寸与比例必须在同一次事务里产出"。

#include <gtest/gtest.h>

#include "editor_bridge/PresentationTargetPolicy.h"

using editor_bridge::PresentationTargetPolicy;

namespace {
constexpr int32_t kHeaderBytes = 16; // ENGINE_HOST_FRAME_PACKET_HEADER_BYTES
} // namespace

// ── 基本：首次请求必重建，且尺寸/比例同步产出 ──────────────────────────
TEST(PresentationTargetPolicy, FirstValidRequestResizes) {
    PresentationTargetPolicy p;
    EXPECT_FALSE(p.IsInitialized());

    auto r = p.Request(1920, 1080);
    EXPECT_EQ(r.decision, PresentationTargetPolicy::Decision::Resize);
    EXPECT_EQ(r.width, 1920);
    EXPECT_EQ(r.height, 1080);
    EXPECT_NEAR(r.aspect, 1920.0f / 1080.0f, 1e-6f);
    EXPECT_TRUE(p.IsInitialized());
}

// ── 去重：拖拽窗口时每帧都来同样尺寸，不应反复重建 ────────────────────
TEST(PresentationTargetPolicy, RepeatedIdenticalSizeIsDeduplicated) {
    PresentationTargetPolicy p;
    p.Request(1280, 720);

    for (int i = 0; i < 500; ++i) {
        auto r = p.Request(1280, 720);
        EXPECT_EQ(r.decision, PresentationTargetPolicy::Decision::Unchanged)
            << "第 " << i << " 次重复请求触发了重建 —— 拖拽窗口会每帧重建一次 FBO";
        EXPECT_EQ(r.width, 1280);
        EXPECT_EQ(r.height, 720);
    }
}

// ── 零尺寸：最小化时不得销毁 GL 资源 ──────────────────────────────────
TEST(PresentationTargetPolicy, ZeroSizeIsRejectedAndKeepsTheLastValidTarget) {
    PresentationTargetPolicy p;
    p.Request(1600, 900);

    for (int32_t bad : {0, -1, -800}) {
        auto r = p.Request(bad, 900);
        EXPECT_EQ(r.decision, PresentationTargetPolicy::Decision::Rejected)
            << "非法宽度 " << bad << " 不该被接受";
        // 关键：保留上一个有效尺寸，而不是变成 0。
        EXPECT_EQ(r.width, 1600) << "非法请求污染了有效尺寸 —— FBO 会被 resize 到 0";
        EXPECT_EQ(r.height, 900);
    }

    auto rh = p.Request(1600, 0);
    EXPECT_EQ(rh.decision, PresentationTargetPolicy::Decision::Rejected);
    EXPECT_EQ(rh.height, 900) << "非法高度污染了有效尺寸";

    // 非法请求之后，策略自身仍处于上一个有效状态。
    EXPECT_EQ(p.Width(), 1600);
    EXPECT_EQ(p.Height(), 900);
}

// ── 恢复：最小化后还原必须能重建回有效尺寸 ────────────────────────────
TEST(PresentationTargetPolicy, RecoversAfterAZeroSizeWindow) {
    PresentationTargetPolicy p;
    p.Request(1600, 900);

    // 最小化
    auto zero = p.Request(0, 0);
    EXPECT_EQ(zero.decision, PresentationTargetPolicy::Decision::Rejected);

    // 还原后通常不是原来的尺寸（用户顺手改了窗口大小）
    auto back = p.Request(1024, 768);
    EXPECT_EQ(back.decision, PresentationTargetPolicy::Decision::Resize)
        << "窗口还原后没有重建 —— 渲染目标会卡在最小化之前的尺寸";
    EXPECT_EQ(back.width, 1024);
    EXPECT_EQ(back.height, 768);
}

// ── 越界：失控尺寸必须在分配之前被挡住 ────────────────────────────────
TEST(PresentationTargetPolicy, OversizedRequestIsRejectedBeforeAllocating) {
    PresentationTargetPolicy p;
    p.Request(1920, 1080);

    const int32_t huge = PresentationTargetPolicy::kMaxDimension + 1;
    auto r = p.Request(huge, 1080);
    EXPECT_EQ(r.decision, PresentationTargetPolicy::Decision::Rejected)
        << "越界宽度未被挡住 —— 会让 FBO 分配几 GB 再失败";
    EXPECT_EQ(r.width, 1920);

    auto r2 = p.Request(1920, huge);
    EXPECT_EQ(r2.decision, PresentationTargetPolicy::Decision::Rejected);
    EXPECT_EQ(r2.height, 1080);
}

// ── 同一事务：尺寸与比例必须一起产出，不允许错配一帧 ──────────────────
TEST(PresentationTargetPolicy, SizeAndAspectComeFromTheSameTransaction) {
    PresentationTargetPolicy p;

    // 每一组尺寸：返回的 aspect 必须精确等于该次 width/height，
    // 而不是上一帧或默认值残留。
    const int32_t sizes[][2] = {{800, 600}, {1920, 1080}, {640, 480},
                                {2560, 1080}, {1000, 1000}, {1281, 721}};
    for (const auto& s : sizes) {
        auto r = p.Request(s[0], s[1]);
        ASSERT_EQ(r.decision, PresentationTargetPolicy::Decision::Resize);
        EXPECT_FLOAT_EQ(r.aspect, static_cast<float>(s[0]) / static_cast<float>(s[1]))
            << "尺寸 (" << s[0] << "," << s[1] << ") 的比例与尺寸不匹配";
        EXPECT_FLOAT_EQ(p.Aspect(), r.aspect);
    }
}

// ── 极端比例不得除零 ──────────────────────────────────────────────────
TEST(PresentationTargetPolicy, AspectIsNeverDividedByZero) {
    PresentationTargetPolicy p; // 尚未初始化
    EXPECT_FLOAT_EQ(p.Aspect(), 0.0f);

    auto r = p.Request(1000, 0); // 非法
    EXPECT_EQ(r.decision, PresentationTargetPolicy::Decision::Rejected);
    EXPECT_FLOAT_EQ(r.aspect, 0.0f) << "非法请求不得产出 NaN 或 Inf";
}

// ── 多尺寸遍历：连续 resize 序列的状态机正确性 ────────────────────────
TEST(PresentationTargetPolicy, SurvivesALongResizeSequence) {
    PresentationTargetPolicy p;
    int resizes = 0;

    // 模拟拖拽窗口：尺寸逐帧变化，中间穿插最小化与越界。
    for (int i = 0; i < 200; ++i) {
        int32_t w = 800 + (i % 37) * 11;
        int32_t h = 600 + (i % 23) * 7;
        if (i % 17 == 0) { w = 0; h = 0; }        // 最小化
        if (i % 29 == 0) { w = -5; }              // 非法
        auto r = p.Request(w, h);

        // 不变式：**一旦有过有效请求**，策略此后任何时刻都持有有效尺寸。
        // 首帧可能直接收到 0×0（模拟窗口一开就是最小化），此时尚未初始化
        // 是正确状态，不该被这条不变式覆盖。
        if (p.IsInitialized()) {
            EXPECT_GT(p.Width(), 0);
            EXPECT_GT(p.Height(), 0);
            EXPECT_LE(p.Width(), PresentationTargetPolicy::kMaxDimension);
        }
        if (r.decision == PresentationTargetPolicy::Decision::Resize) {
            ++resizes;
            EXPECT_GT(r.width, 0);
            EXPECT_GT(r.height, 0);
        }
    }
    EXPECT_TRUE(p.IsInitialized()) << "整个序列之后策略仍未初始化";
    EXPECT_GT(resizes, 100) << "连续变化应当产生大量重建，说明去重没把真变化也吃掉";
}

// ── packet 容量必须按运行时尺寸算，不能用编译期 800×600 上界 ──────────
TEST(PresentationTargetPolicy, RequiredPacketBytesTracksTheRuntimeSize) {
    using P = PresentationTargetPolicy;

    EXPECT_EQ(P::RequiredPacketBytes(800, 600, kHeaderBytes),
              static_cast<int64_t>(kHeaderBytes) + 800LL * 600LL * 4);
    EXPECT_EQ(P::RequiredPacketBytes(1920, 1080, kHeaderBytes),
              static_cast<int64_t>(kHeaderBytes) + 1920LL * 1080LL * 4);

    // 非默认尺寸必须算出**更大**的容量 —— 这正是旧代码会误判的地方：
    // 它拿编译期 800×600 的上界去校验任何尺寸的帧。
    EXPECT_GT(P::RequiredPacketBytes(1920, 1080, kHeaderBytes),
              P::RequiredPacketBytes(800, 600, kHeaderBytes));

    // 非法尺寸不产生容量，避免负数被当成 buffer 下界。
    EXPECT_EQ(P::RequiredPacketBytes(0, 0, kHeaderBytes), 0);
    EXPECT_EQ(P::RequiredPacketBytes(-1, 600, kHeaderBytes), 0);

    // 上界内的最大合法尺寸：16384*16384*4 = 1,073,741,824，仍在 int32 范围内。
    // 记下来是因为它决定了 FETCH 路径可以用 int32 表达容量而不必全用 64 位。
    const int64_t maxLegal = P::RequiredPacketBytes(P::kMaxDimension,
                                                     P::kMaxDimension, kHeaderBytes);
    EXPECT_EQ(maxLegal, 16LL + 16384LL * 16384LL * 4);
    EXPECT_LT(maxLegal, static_cast<int64_t>(INT32_MAX))
        << "若将来抬高 kMaxDimension，下面这条 32 位溢出的检查才需要生效";

    // 超上界的输入依然按 64 位精确计算（策略层不做 clamp —— clamp 是 GL 层的职责，
    // 这里只保证算术本身不溢出，这样上层可以区分"被上界挡下"和"算错了"）。
    const int64_t beyond = P::RequiredPacketBytes(100000, 100000, kHeaderBytes);
    EXPECT_EQ(beyond, 16LL + 100000LL * 100000LL * 4);
    EXPECT_GT(beyond, static_cast<int64_t>(INT32_MAX))
        << "超上界尺寸的容量计算发生了 32 位截断";
}

// ── 默认尺寸沿用既有常量，仅作为初始值 ────────────────────────────────
TEST(PresentationTargetPolicy, DefaultMatchesTheLegacyBootstrapConstants) {
    // 与 capi.h 的 ENGINE_HOST_PRESENTATION_WIDTH/HEIGHT 对齐。
    EXPECT_EQ(PresentationTargetPolicy::kDefaultWidth, 800);
    EXPECT_EQ(PresentationTargetPolicy::kDefaultHeight, 600);

    PresentationTargetPolicy p;
    EXPECT_EQ(p.Request(PresentationTargetPolicy::kDefaultWidth,
                        PresentationTargetPolicy::kDefaultHeight).decision,
              PresentationTargetPolicy::Decision::Resize);
}
