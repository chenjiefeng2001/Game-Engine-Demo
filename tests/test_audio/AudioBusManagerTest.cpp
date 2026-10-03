// P1: Audio 子系统测试 —— AudioBusManager (第一批)
//
// 依据 docs/Engine-Capability-Audit.md：Audio 13+ 文件、零测试。
// 先做只读审计，结论：AudioBusManager / SpatialAudioManager /
// AudioAssetManager 三个实现**零 AL/ALC 调用点**，完全不依赖 OpenAL 设备，
// 因此可在无音频硬件的 CI 上测试；AudioEngine(27) / AudioEffect(4) 需要设备，
// 放到后续批次。
//
// 本单元只覆盖 AudioBusManager。断言全部为不变量：
//   - CreateBus 幂等：同名重复创建返回同一指针，不产生第二条总线
//   - GetBus 对未知名称返回 nullptr
//   - SetBusVolume 钳制到 [0,1]
//   - sends 与 sendLevels 始终成对（长度一致、索引对应）
//   - ApplyBusProcessing 对 nullptr / 0 帧安全
//   - ApplyBusProcessing 的音量、mute、bypass 语义
//   - ApplyBusProcessing 幂等：重复调用不得改变结果
//
// 注意 API 实况（读源码确认，未猜测）：
//   - AudioBusConfig::sends 是 std::vector<AudioBusConfig*>，指向
//     unordered_map 内的元素；unordered_map 对元素保证指针稳定，
//     且 RemoveBus 会先摘掉指向被删总线的 send，故该设计是安全的。
//   - RemoveBus 中 sendLevels 的擦除索引取自 erase 之后的
//     std::distance(sends.begin(), it)，该值等于被删元素的**原**索引，
//     故索引同步是正确的（已逐行核对，非缺陷）。
//   - ApplyBusProcessing 分三步：①逐总线音量 ②效果器湿/干混合 ③辅助发送。
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Engine/Audio/AudioEngine.h"

using namespace Engine;
using namespace Engine::Audio;

namespace {

constexpr uint32_t kSampleRate = 44100;

// 造一个 numFrames 帧的立体声缓冲，全部填 1.0，便于观察增益。
std::vector<float> MakeBuffer(size_t numFrames, float value = 1.0f)
{
    return std::vector<float>(numFrames * 2, value);
}

} // namespace

// ── 总线生命周期 ───────────────────────────────────────────────────────
TEST(AudioBusManagerTest, CreateBusReturnsUsableConfigWithDefaults)
{
    AudioBusManager mgr;
    AudioBusConfig* bus = mgr.CreateBus("Master");
    ASSERT_NE(bus, nullptr);
    EXPECT_EQ(bus->name, "Master");
    EXPECT_FLOAT_EQ(bus->volume, 1.0f);
    EXPECT_FLOAT_EQ(bus->pitch, 1.0f);
    EXPECT_FALSE(bus->muted);
    EXPECT_FALSE(bus->isBypassed);
    EXPECT_TRUE(bus->sends.empty());
}

TEST(AudioBusManagerTest, CreateBusIsIdempotentForSameName)
{
    AudioBusManager mgr;
    AudioBusConfig* a = mgr.CreateBus("SFX");
    AudioBusConfig* b = mgr.CreateBus("SFX");
    // 同名重复创建必须返回同一对象，而不是插入第二条
    EXPECT_EQ(a, b) << "同名总线重复创建返回了不同对象";

    a->volume = 0.25f;
    EXPECT_FLOAT_EQ(mgr.CreateBus("SFX")->volume, 0.25f)
        << "重复创建覆盖了已有总线的状态";
}

TEST(AudioBusManagerTest, GetBusFindsExistingAndRejectsUnknown)
{
    AudioBusManager mgr;
    AudioBusConfig* created = mgr.CreateBus("Music");
    EXPECT_EQ(mgr.GetBus("Music"), created);
    EXPECT_EQ(mgr.GetBus("Nope"), nullptr);
}

TEST(AudioBusManagerTest, RemoveBusMakesItUnreachable)
{
    AudioBusManager mgr;
    mgr.CreateBus("Temp");
    ASSERT_NE(mgr.GetBus("Temp"), nullptr);
    mgr.RemoveBus("Temp");
    EXPECT_EQ(mgr.GetBus("Temp"), nullptr);
}

TEST(AudioBusManagerTest, RemoveBusOnUnknownNameIsSafe)
{
    AudioBusManager mgr;
    EXPECT_NO_THROW(mgr.RemoveBus("never-existed"));
    EXPECT_NO_THROW(mgr.ApplyBusProcessing(nullptr, 0, kSampleRate));
}

TEST(AudioBusManagerTest, IndependentBusesCoexist)
{
    AudioBusManager mgr;
    AudioBusConfig* a = mgr.CreateBus("A");
    AudioBusConfig* b = mgr.CreateBus("B");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    EXPECT_EQ(mgr.GetBus("A"), a);
    EXPECT_EQ(mgr.GetBus("B"), b);
}

// ── 音量与静音 ─────────────────────────────────────────────────────────
TEST(AudioBusManagerTest, SetBusVolumeStoresValue)
{
    AudioBusManager mgr;
    mgr.CreateBus("SFX");
    mgr.SetBusVolume("SFX", 0.5f);
    EXPECT_FLOAT_EQ(mgr.GetBus("SFX")->volume, 0.5f);
}

TEST(AudioBusManagerTest, SetBusVolumeClampsToUnitInterval)
{
    AudioBusManager mgr;
    mgr.CreateBus("SFX");

    mgr.SetBusVolume("SFX", 5.0f);
    EXPECT_FLOAT_EQ(mgr.GetBus("SFX")->volume, 1.0f) << "超过 1 的音量未被钳制";

    mgr.SetBusVolume("SFX", -3.0f);
    EXPECT_FLOAT_EQ(mgr.GetBus("SFX")->volume, 0.0f) << "负音量未被钳制";
}

TEST(AudioBusManagerTest, SetBusVolumeOnUnknownBusIsIgnored)
{
    AudioBusManager mgr;
    EXPECT_NO_THROW(mgr.SetBusVolume("ghost", 0.5f));
    EXPECT_EQ(mgr.GetBus("ghost"), nullptr);
}

TEST(AudioBusManagerTest, MuteBusTogglesFlag)
{
    AudioBusManager mgr;
    mgr.CreateBus("SFX");
    mgr.MuteBus("SFX", true);
    EXPECT_TRUE(mgr.GetBus("SFX")->muted);
    mgr.MuteBus("SFX", false);
    EXPECT_FALSE(mgr.GetBus("SFX")->muted);
}

// ── 辅助发送（sends / sendLevels 成对不变量）────────────────────────────
TEST(AudioBusManagerTest, AddSendRecordsDestinationAndLevel)
{
    AudioBusManager mgr;
    mgr.CreateBus("SFX");
    mgr.CreateBus("Reverb");
    mgr.AddSend("SFX", "Reverb", 0.4f);

    AudioBusConfig* sfx = mgr.GetBus("SFX");
    ASSERT_EQ(sfx->sends.size(), 1u);
    EXPECT_EQ(sfx->sends[0], mgr.GetBus("Reverb"));
    ASSERT_EQ(sfx->sendLevels.size(), 1u);
    EXPECT_FLOAT_EQ(sfx->sendLevels[0], 0.4f);
}

TEST(AudioBusManagerTest, SendsAndSendLevelsStayPairedInLength)
{
    AudioBusManager mgr;
    mgr.CreateBus("A");
    mgr.CreateBus("B");
    mgr.CreateBus("C");
    mgr.AddSend("A", "B", 0.1f);
    mgr.AddSend("A", "C", 0.2f);

    AudioBusConfig* a = mgr.GetBus("A");
    ASSERT_EQ(a->sends.size(), 2u);
    EXPECT_EQ(a->sends.size(), a->sendLevels.size())
        << "sends 与 sendLevels 长度失配";
}

TEST(AudioBusManagerTest, RemovingMiddleSendKeepsLevelAlignment)
{
    // 删中间那个 send 后，剩下的 sendLevels 必须是"剩下那个"的电平，
    // 而非错位成别的值。
    AudioBusManager mgr;
    mgr.CreateBus("A");
    mgr.CreateBus("B");
    mgr.CreateBus("C");
    mgr.AddSend("A", "B", 0.11f);
    mgr.AddSend("A", "C", 0.22f);

    mgr.RemoveSend("A", "B");
    AudioBusConfig* a = mgr.GetBus("A");
    ASSERT_EQ(a->sends.size(), 1u);
    ASSERT_EQ(a->sendLevels.size(), 1u) << "sendLevels 未同步擦除";
    EXPECT_EQ(a->sends[0], mgr.GetBus("C"));
    EXPECT_FLOAT_EQ(a->sendLevels[0], 0.22f)
        << "sendLevels 索引错位：残留了被删除 send 的电平";
}

TEST(AudioBusManagerTest, RemoveSendClearsPointersBeforeBusRemoval)
{
    // RemoveBus 必须先摘掉指向它的 send，否则留下悬垂指针
    AudioBusManager mgr;
    mgr.CreateBus("A");
    mgr.CreateBus("Gone");
    mgr.AddSend("A", "Gone", 0.5f);

    mgr.RemoveBus("Gone");
    AudioBusConfig* a = mgr.GetBus("A");
    EXPECT_TRUE(a->sends.empty()) << "被删总线仍留在 sends 中（悬垂指针）";
    EXPECT_EQ(a->sends.size(), a->sendLevels.size());
}

TEST(AudioBusManagerTest, AddSendOnUnknownBusIsSafe)
{
    AudioBusManager mgr;
    mgr.CreateBus("Only");
    EXPECT_NO_THROW(mgr.AddSend("Only", "missing", 0.5f));
    EXPECT_NO_THROW(mgr.RemoveSend("Only", "missing"));
}

// ── ApplyBusProcessing：缓冲区安全 ─────────────────────────────────────
TEST(AudioBusManagerTest, ProcessingRejectsNullBufferAndZeroFrames)
{
    AudioBusManager mgr;
    mgr.CreateBus("Master");
    EXPECT_NO_THROW(mgr.ApplyBusProcessing(nullptr, 16, kSampleRate));

    auto buf = MakeBuffer(16);
    EXPECT_NO_THROW(mgr.ApplyBusProcessing(buf.data(), 0, kSampleRate));
    // 0 帧必须原样不动
    for (float s : buf) EXPECT_FLOAT_EQ(s, 1.0f);
}

// ── ApplyBusProcessing：增益语义 ───────────────────────────────────────
TEST(AudioBusManagerTest, SingleBusVolumeScalesBuffer)
{
    AudioBusManager mgr;
    mgr.CreateBus("Master");
    mgr.SetBusVolume("Master", 0.5f);

    auto buf = MakeBuffer(8);
    mgr.ApplyBusProcessing(buf.data(), 8, kSampleRate);
    for (float s : buf) EXPECT_FLOAT_EQ(s, 0.5f);
}

TEST(AudioBusManagerTest, MutedBusLeavesBufferUntouched)
{
    AudioBusManager mgr;
    mgr.CreateBus("Master");
    mgr.SetBusVolume("Master", 0.1f);
    mgr.MuteBus("Master", true);

    auto buf = MakeBuffer(8);
    mgr.ApplyBusProcessing(buf.data(), 8, kSampleRate);
    for (float s : buf) EXPECT_FLOAT_EQ(s, 1.0f) << "静音总线仍在施加增益";
}

TEST(AudioBusManagerTest, BypassedBusLeavesBufferUntouched)
{
    AudioBusManager mgr;
    mgr.CreateBus("Master");
    mgr.SetBusVolume("Master", 0.1f);
    mgr.GetBus("Master")->isBypassed = true;

    auto buf = MakeBuffer(8);
    mgr.ApplyBusProcessing(buf.data(), 8, kSampleRate);
    for (float s : buf) EXPECT_FLOAT_EQ(s, 1.0f) << "bypass 总线仍在施加增益";
}

TEST(AudioBusManagerTest, UnitVolumeIsIdentity)
{
    AudioBusManager mgr;
    mgr.CreateBus("Master");
    mgr.SetBusVolume("Master", 1.0f);

    auto buf = MakeBuffer(8);
    mgr.ApplyBusProcessing(buf.data(), 8, kSampleRate);
    for (float s : buf) EXPECT_FLOAT_EQ(s, 1.0f);
}

// ── 增益连乘语义（观察，非缺陷）────────────────────────────────────────
TEST(AudioBusManagerTest, RepeatedCallsCompoundBusVolumeGain)
{
    // 观察（不是缺陷）：对同一缓冲连续施加增益会按增益连乘，
    // 这是 in-place 增益的正常语义，不构成幂等性要求。
    // 早先这里曾错误地断言"重复处理应幂等"，属于把增益当成了状态机。
    AudioBusManager mgr;
    mgr.CreateBus("Master");
    mgr.SetBusVolume("Master", 0.75f);

    auto buf = MakeBuffer(8);
    mgr.ApplyBusProcessing(buf.data(), 8, kSampleRate);
    EXPECT_FLOAT_EQ(buf[0], 0.75f);
    mgr.ApplyBusProcessing(buf.data(), 8, kSampleRate);
    EXPECT_NEAR(buf[0], 0.75 * 0.75, 1e-5f) << "增益未按预期连乘";
}

TEST(AudioBusManagerTest, BusVolumesCompoundAcrossBuses)
{
    // 观察（待产品决策，非缺陷）：步骤 1 对**每条**总线都把同一个缓冲
    // 乘一次该总线的 volume，因此 N 条总线得到的是增益的**连乘**，
    // 而不是各总线贡献的叠加。两条 0.5 的总线 ⇒ 0.25。
    // 由于本实现没有 per-bus 输入路由（源码自述为简化实现），
    // 无法从现有 API 判定"连乘"是否符合作者意图，故此处只记录实测值。
    AudioBusManager mgr;
    mgr.CreateBus("A");
    mgr.CreateBus("B");
    mgr.SetBusVolume("A", 0.5f);
    mgr.SetBusVolume("B", 0.5f);

    auto buf = MakeBuffer(4);
    mgr.ApplyBusProcessing(buf.data(), 4, kSampleRate);
    EXPECT_NEAR(buf[0], 0.25f, 1e-5f)
        << "总线音量行为已变更：若改为叠加语义，此处需改为 0.5";
}

// ── K4 回归：sends 不得改写 routing state ──────────────────────────────
//
// 历史缺陷：步骤 3 曾执行
//     destBus->volume += bus.volume * sendLevel * 0.1f;
// 把目标总线的**持久 volume** 当作累加目标，于是每调用一次处理音量被永久
// 抬高（实测 1.0 → 1.1 → 1.3），并绕过 SetBusVolume 的 [0,1] 钳制。
// 修复：发送贡献改为局部增益，不写回任何持久 routing state。
// 下面断言的是 contract（处理不改状态、状态不越界），不是缺陷指纹。
TEST(AudioBusManagerTest, ProcessingWithSendsDoesNotMutateBusVolume)
{
    AudioBusManager mgr;
    mgr.CreateBus("Src");
    mgr.CreateBus("Dst");
    mgr.SetBusVolume("Src", 1.0f);
    mgr.SetBusVolume("Dst", 1.0f);
    mgr.AddSend("Src", "Dst", 1.0f);

    const float before = mgr.GetBus("Dst")->volume;
    auto buf = MakeBuffer(4);
    mgr.ApplyBusProcessing(buf.data(), 4, kSampleRate);
    EXPECT_FLOAT_EQ(mgr.GetBus("Dst")->volume, before)
        << "K4: 处理改写了目标总线的持久 volume";
}

TEST(AudioBusManagerTest, ProcessingRepeatedlyWithSendsDoesNotInflateBusVolume)
{
    AudioBusManager mgr;
    mgr.CreateBus("Src");
    mgr.CreateBus("Dst");
    mgr.SetBusVolume("Src", 1.0f);
    mgr.SetBusVolume("Dst", 1.0f);
    mgr.AddSend("Src", "Dst", 1.0f);

    auto buf = MakeBuffer(4);
    mgr.ApplyBusProcessing(buf.data(), 4, kSampleRate);
    const float volAfterFirst = mgr.GetBus("Dst")->volume;

    mgr.ApplyBusProcessing(buf.data(), 4, kSampleRate);
    mgr.ApplyBusProcessing(buf.data(), 4, kSampleRate);
    const float volAfterThird = mgr.GetBus("Dst")->volume;

    EXPECT_FLOAT_EQ(volAfterThird, volAfterFirst)
        << "K4: 重复处理使目标总线音量持续增长";
    EXPECT_FLOAT_EQ(volAfterThird, 1.0f) << "K4: send 路径绕过了 [0,1] 钳制";
    EXPECT_LE(volAfterThird, 1.0f) << "K4: 音量越界";
}

TEST(AudioBusManagerTest, ProcessingWithSendsHasNoPersistentSideEffects)
{
    // 反复处理后，整张总线图的持久状态必须与处理前完全一致
    AudioBusManager mgr;
    mgr.CreateBus("Src");
    mgr.CreateBus("Dst");
    mgr.CreateBus("Rev");
    mgr.SetBusVolume("Src", 0.8f);
    mgr.SetBusVolume("Dst", 0.6f);
    mgr.SetBusVolume("Rev", 0.4f);
    mgr.AddSend("Src", "Dst", 0.5f);
    mgr.AddSend("Dst", "Rev", 0.25f);

    const float srcBefore = mgr.GetBus("Src")->volume;
    const float dstBefore = mgr.GetBus("Dst")->volume;
    const float revBefore = mgr.GetBus("Rev")->volume;

    auto buf = MakeBuffer(4);
    for (int i = 0; i < 8; ++i) {
        mgr.ApplyBusProcessing(buf.data(), 4, kSampleRate);
    }

    EXPECT_FLOAT_EQ(mgr.GetBus("Src")->volume, srcBefore);
    EXPECT_FLOAT_EQ(mgr.GetBus("Dst")->volume, dstBefore)
        << "K4: 多级 send 下目标总线音量被改写";
    EXPECT_FLOAT_EQ(mgr.GetBus("Rev")->volume, revBefore);
}

TEST(AudioBusManagerTest, SendLevelsSurviveProcessingUnchanged)
{
    // sendLevels 属于 routing state，处理不得改动
    AudioBusManager mgr;
    mgr.CreateBus("Src");
    mgr.CreateBus("Dst");
    mgr.AddSend("Src", "Dst", 0.33f);

    auto buf = MakeBuffer(4);
    mgr.ApplyBusProcessing(buf.data(), 4, kSampleRate);
    ASSERT_EQ(mgr.GetBus("Src")->sendLevels.size(), 1u);
    EXPECT_FLOAT_EQ(mgr.GetBus("Src")->sendLevels[0], 0.33f);
}

TEST(AudioBusManagerTest, MutedSendDestinationIsStillNotMutated)
{
    // 静音的目标总线同样不得被写入 volume
    AudioBusManager mgr;
    mgr.CreateBus("Src");
    mgr.CreateBus("Dst");
    mgr.SetBusVolume("Dst", 0.9f);
    mgr.MuteBus("Dst", true);
    mgr.AddSend("Src", "Dst", 1.0f);

    const float before = mgr.GetBus("Dst")->volume;
    auto buf = MakeBuffer(4);
    mgr.ApplyBusProcessing(buf.data(), 4, kSampleRate);
    EXPECT_FLOAT_EQ(mgr.GetBus("Dst")->volume, before);
    EXPECT_TRUE(mgr.GetBus("Dst")->muted) << "处理不得改变静音标志";
}
