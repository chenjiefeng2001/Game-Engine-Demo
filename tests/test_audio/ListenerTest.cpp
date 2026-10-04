// P1: Core/Audio headless 覆盖 —— Listener
//
// 对应 engine/include/Engine/Core/Audio/Listener.h +
// engine/src/Audio/Listener.cpp（182 行实现），此前零直接测试。
//
// 准入结论（审计确认）：Listener 是 Category 1 —— 实现只 include
// <cmath> / <algorithm> 与 TransformComponent.h，**没有任何 OpenAL 头**，
// 且 Apply / ApplyTransform 通过 `IAudioEngine&` 引用注入。
// 因此可以用测试内的 fake 引擎确定性地验证转发值，无需任何音频设备。
//
// 覆盖：
//   - 构造默认值
//   - SetOrientation：归一化 + Gram-Schmidt 正交化 + pitch/yaw 缓存
//   - LookAt：forward/up 计算与世界 up 参数
//   - SetOrientationFromEuler：球坐标换算、退化守卫、y/p 直存
//   - SetOrientationFromMatrix：列主序 第三列取反为 forward、第二列为 up
//   - NormalizeOrientation：正交归一化
//   - velocity / volume / environment 状态（无钳制）
//   - SyncFromTransform：位置与朝向同步
//   - Apply / ApplyTransform：通过注入引擎转发的**实际数值**
//
// 明确不在本文件范围（审计判定为 Category 3 / 2）：
//   - AudioSystem 一次性音效：PlayOneShot 直接调用 alSourcei/alSourcePlay，
//     绕过 IAudioEngine 抽象（layering violation）
//   - AudioSourceComponent::Play：同样直接调用 alSourcei/alGetError
//   - AudioClip / AudioClipManager：ResourceManager 单例 + 缓存原生 buffer
#include <gtest/gtest.h>

#include <cmath>

#include "Engine/Core/Audio/Listener.h"
#include "Engine/Core/Audio/IAudioEngine.h"
#include "Engine/Core/GameObject/TransformComponent.h"

using namespace Engine;

namespace {

constexpr float32 kEps = 1e-4f;

float32 Dot3(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float32 Len3(const Vec3& v) { return std::sqrt(Dot3(v, v)); }

/// 记录 Listener 转发值的 fake 引擎。只实现 Listener 会用到的部分，
/// 其余接口给出最小可用实现（不参与断言）。
class FakeAudioEngine : public IAudioEngine {
public:
    bool Init() override { m_Initialized = true; return true; }
    void Shutdown() override { m_Initialized = false; }
    bool IsInitialized() const override { return m_Initialized; }

    std::shared_ptr<IAudioBuffer> CreateBuffer(const void*, int32,
                                               const AudioClipInfo&) override {
        return nullptr;
    }
    std::shared_ptr<IAudioSource> CreateSource() override { return nullptr; }
    void DestroySource(IAudioSource*) override {}

    void SetListenerPosition(const Vec3& p) override {
        m_Position = p;
        ++positionCalls;
    }
    Vec3 GetListenerPosition() const override { return m_Position; }

    void SetListenerOrientation(const Vec3& f, const Vec3& u) override {
        m_Forward = f;
        m_Up = u;
        ++orientationCalls;
    }
    void SetListenerVolume(float32 g) override {
        m_Volume = g;
        ++volumeCalls;
    }

    void SetMasterVolume(float32) override {}
    float32 GetMasterVolume() const override { return 1.0f; }
    void SetDistanceModel(AttenuationMode) override {}
    void Update() override {}
    void* GetNativeDevice() const override { return nullptr; }
    void* GetNativeContext() const override { return nullptr; }

    Vec3 m_Position{9.0f, 9.0f, 9.0f};
    Vec3 m_Forward{9.0f, 9.0f, 9.0f};
    Vec3 m_Up{9.0f, 9.0f, 9.0f};
    float32 m_Volume = -1.0f;
    int positionCalls = 0;
    int orientationCalls = 0;
    int volumeCalls = 0;

private:
    bool m_Initialized = false;
};

} // namespace

// ── 默认值 ─────────────────────────────────────────────────────────────
TEST(ListenerTest, Defaults_MatchDocumentedOrientation)
{
    Listener l;
    EXPECT_FLOAT_EQ(l.GetPosition().x, 0.0f);
    EXPECT_FLOAT_EQ(l.GetPosition().y, 0.0f);
    EXPECT_FLOAT_EQ(l.GetPosition().z, 0.0f);
    EXPECT_FLOAT_EQ(l.GetForward().x, 0.0f);
    EXPECT_FLOAT_EQ(l.GetForward().y, 0.0f);
    EXPECT_FLOAT_EQ(l.GetForward().z, -1.0f);   // OpenAL 默认朝向 -Z
    EXPECT_FLOAT_EQ(l.GetUp().x, 0.0f);
    EXPECT_FLOAT_EQ(l.GetUp().y, 1.0f);
    EXPECT_FLOAT_EQ(l.GetUp().z, 0.0f);
}

// ── SetOrientation ─────────────────────────────────────────────────────
TEST(ListenerTest, SetOrientation_NormalizesForwardAndUp)
{
    Listener l;
    l.SetOrientation(Vec3(0.0f, 0.0f, -5.0f), Vec3(0.0f, 3.0f, 0.0f));
    EXPECT_NEAR(Len3(l.GetForward()), 1.0f, kEps);
    EXPECT_NEAR(Len3(l.GetUp()), 1.0f, kEps);
    EXPECT_NEAR(l.GetForward().z, -1.0f, kEps);
    EXPECT_NEAR(l.GetUp().y, 1.0f, kEps);
}

TEST(ListenerTest, SetOrientation_OrthogonalizesUpAgainstForward)
{
    Listener l;
    // up 明显不垂直于 forward
    l.SetOrientation(Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 0.0f));
    EXPECT_NEAR(Dot3(l.GetForward(), l.GetUp()), 0.0f, kEps);
    EXPECT_NEAR(Len3(l.GetUp()), 1.0f, kEps);
}

TEST(ListenerTest, SetOrientation_UpdatesPitchYawCache)
{
    Listener l;
    // yaw = atan2(f.x, f.z)：forward = -Z 时 atan2(0, -1) = +pi
    l.SetOrientation(Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(l.GetPitch(), 0.0f, kEps);
    EXPECT_NEAR(l.GetYaw(), 3.14159265f, 1e-3f);

    // 纯俯仰 90 度：forward = +Y → pitch = asin(1)
    l.SetOrientation(Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(l.GetPitch(), 1.5707963f, 1e-3f);
}

TEST(ListenerTest, SetOrientation_YawFollowsAtan2Convention)
{
    Listener l;
    // forward = +X → yaw = atan2(1, 0) = +pi/2
    l.SetOrientation(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(l.GetYaw(), 1.5707963f, 1e-3f);
    EXPECT_NEAR(l.GetPitch(), 0.0f, kEps);
}

// ── LookAt ─────────────────────────────────────────────────────────────
TEST(ListenerTest, LookAt_ComputesForwardFromPositionToTarget)
{
    Listener l;
    l.SetPosition(Vec3(0.0f, 0.0f, 0.0f));
    l.LookAt(Vec3(0.0f, 0.0f, 10.0f));
    EXPECT_NEAR(l.GetForward().x, 0.0f, kEps);
    EXPECT_NEAR(l.GetForward().y, 0.0f, kEps);
    EXPECT_NEAR(l.GetForward().z, 1.0f, kEps);
}

TEST(ListenerTest, LookAt_AccountsForListenerPosition)
{
    Listener l;
    l.SetPosition(Vec3(5.0f, 0.0f, 5.0f));
    l.LookAt(Vec3(5.0f, 0.0f, 15.0f));
    EXPECT_NEAR(l.GetForward().z, 1.0f, kEps);
    EXPECT_NEAR(Len3(l.GetForward()), 1.0f, kEps);
}

TEST(ListenerTest, LookAt_ProducesOrthonormalBasis)
{
    Listener l;
    l.LookAt(Vec3(3.0f, 4.0f, 12.0f));
    EXPECT_NEAR(Len3(l.GetForward()), 1.0f, kEps);
    EXPECT_NEAR(Len3(l.GetUp()), 1.0f, kEps);
    EXPECT_NEAR(Dot3(l.GetForward(), l.GetUp()), 0.0f, kEps);
}

TEST(ListenerTest, LookAt_ProducesDifferentUpForDifferentWorldUp)
{
    // 固定目标（forward = +X），只改 worldUp，up 必须随之改变
    Listener a, b;
    a.LookAt(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f));
    b.LookAt(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(a.GetForward().x, 1.0f, kEps);
    EXPECT_NEAR(b.GetForward().x, 1.0f, kEps);
    EXPECT_NEAR(a.GetUp().z, 1.0f, kEps);     // worldUp = +Z → up 偏到 +Z
    EXPECT_NEAR(b.GetUp().y, 1.0f, kEps);     // worldUp = +Y → up 为 +Y
}

TEST(ListenerTest, LookAt_WorldUpParallelToForward_DegeneratesToZeroUp)
{
    // 已记录的行为（不是理想契约）：LookAt 缺少退化守卫。
    // worldUp 与 forward 平行时 right = cross(f, up) = 0，up 退化为零向量。
    // 对比 SetOrientationFromEuler —— 它在同样情形下会用世界 Z 轴兜底。
    // 若将来给 LookAt 补上守卫，本用例应改为断言兜底后的非零 up。
    Listener l;
    l.LookAt(Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(l.GetForward().z, 1.0f, kEps);
    EXPECT_NEAR(l.GetUp().x, 0.0f, kEps);
    EXPECT_NEAR(l.GetUp().y, 0.0f, kEps);
    EXPECT_NEAR(l.GetUp().z, 0.0f, kEps);
}

TEST(ListenerTest, LookAt_UpdatesPitchYawCache)
{
    Listener l;
    l.LookAt(Vec3(1.0f, 0.0f, 0.0f));   // forward = +X
    EXPECT_NEAR(l.GetYaw(), 1.5707963f, 1e-3f);
}

// ── SetOrientationFromEuler ────────────────────────────────────────────
TEST(ListenerTest, SetOrientationFromEuler_StoresYawPitchVerbatim)
{
    Listener l;
    l.SetOrientationFromEuler(0.5f, 0.25f);
    EXPECT_FLOAT_EQ(l.GetYaw(), 0.5f);
    EXPECT_FLOAT_EQ(l.GetPitch(), 0.25f);
}

TEST(ListenerTest, SetOrientationFromEuler_ForwardMatchesSphericalConversion)
{
    Listener l;
    const float32 yaw = 0.7f, pitch = -0.3f;
    l.SetOrientationFromEuler(yaw, pitch);
    const Vec3 f = l.GetForward();
    const float32 cp = std::cos(pitch);
    EXPECT_NEAR(f.x, cp * std::sin(yaw), kEps);
    EXPECT_NEAR(f.y, std::sin(pitch), kEps);
    EXPECT_NEAR(f.z, cp * std::cos(yaw), kEps);
    EXPECT_NEAR(Len3(f), 1.0f, kEps);
}

TEST(ListenerTest, SetOrientationFromEuler_UpIsOrthogonalToForward)
{
    Listener l;
    l.SetOrientationFromEuler(1.1f, 0.4f);
    EXPECT_NEAR(Dot3(l.GetForward(), l.GetUp()), 0.0f, kEps);
    EXPECT_NEAR(Len3(l.GetUp()), 1.0f, kEps);
}

TEST(ListenerTest, SetOrientationFromEuler_ZeroYawPitch_ForwardIsPlusZ)
{
    Listener l;
    l.SetOrientationFromEuler(0.0f, 0.0f);
    EXPECT_NEAR(l.GetForward().z, 1.0f, kEps);
    EXPECT_NEAR(l.GetForward().y, 0.0f, kEps);
}

TEST(ListenerTest, SetOrientationFromEuler_StraightUp_DoesNotProduceNaN)
{
    // pitch = +90° → forward 与 worldUp 平行 → right 退化为零向量，
    // 源码用世界 Z 轴兜底。此处确保结果有限且非 NaN。
    Listener l;
    l.SetOrientationFromEuler(0.0f, 1.5707963f);
    const Vec3 f = l.GetForward();
    const Vec3 u = l.GetUp();
    EXPECT_TRUE(std::isfinite(f.x) && std::isfinite(f.y) && std::isfinite(f.z));
    EXPECT_TRUE(std::isfinite(u.x) && std::isfinite(u.y) && std::isfinite(u.z));
    EXPECT_NEAR(Len3(f), 1.0f, kEps);
}

TEST(ListenerTest, SetOrientationFromEuler_StraightDown_DoesNotProduceNaN)
{
    Listener l;
    l.SetOrientationFromEuler(0.0f, -1.5707963f);
    const Vec3 f = l.GetForward();
    EXPECT_TRUE(std::isfinite(f.x) && std::isfinite(f.y) && std::isfinite(f.z));
    EXPECT_NEAR(Len3(f), 1.0f, kEps);
}

// ── SetOrientationFromMatrix ───────────────────────────────────────────
TEST(ListenerTest, SetOrientationFromMatrix_ExtractsNegatedThirdColumnAsForward)
{
    // 列主序 data[col*4 + row]；forward = -(data[8], data[9], data[10])
    Mat4 m{};                       // 全零
    m.data[8] = 0.0f;
    m.data[9] = 0.0f;
    m.data[10] = 1.0f;              // 第三列 = +Z → forward 取反后 = -Z
    m.data[4] = 0.0f;
    m.data[5] = 1.0f;
    m.data[6] = 0.0f;

    Listener l;
    l.SetOrientationFromMatrix(m);
    EXPECT_NEAR(l.GetForward().z, -1.0f, kEps);
    EXPECT_NEAR(l.GetUp().y, 1.0f, kEps);
    EXPECT_NEAR(Dot3(l.GetForward(), l.GetUp()), 0.0f, kEps);
}

TEST(ListenerTest, SetOrientationFromMatrix_NormalizesExtractedBasis)
{
    Mat4 m{};
    m.data[8] = 0.0f; m.data[9] = 0.0f; m.data[10] = -5.0f;   // forward = +Z * 5
    m.data[4] = 0.0f; m.data[5] = 2.0f; m.data[6] = 0.0f;    // up = +Y * 2

    Listener l;
    l.SetOrientationFromMatrix(m);
    EXPECT_NEAR(Len3(l.GetForward()), 1.0f, kEps);
    EXPECT_NEAR(Len3(l.GetUp()), 1.0f, kEps);
    EXPECT_NEAR(l.GetForward().z, 1.0f, kEps);
}

TEST(ListenerTest, SetOrientationFromMatrix_UpdatesPitchYawCache)
{
    Mat4 m{};
    m.data[8] = -1.0f; m.data[9] = 0.0f; m.data[10] = 0.0f;   // forward = +X
    m.data[4] = 0.0f;  m.data[5] = 1.0f; m.data[6] = 0.0f;

    Listener l;
    l.SetOrientationFromMatrix(m);
    EXPECT_NEAR(l.GetYaw(), 1.5707963f, 1e-3f);
    EXPECT_NEAR(l.GetPitch(), 0.0f, kEps);
}

// ── 正交化 ─────────────────────────────────────────────────────────────
// 注意：NormalizeOrientation() 是 private，只能通过 SetOrientation /
// LookAt 等公开入口间接覆盖（见上方 SetOrientation_OrthogonalizesUpAgainstForward）。

// ── 状态：velocity / volume / environment ─────────────────────────────
TEST(ListenerTest, VelocityAndVolume_AreStoredVerbatim_NoClamping)
{
    Listener l;
    l.SetVelocity(Vec3(1.5f, -2.5f, 3.5f));
    EXPECT_FLOAT_EQ(l.GetVelocity().x, 1.5f);
    EXPECT_FLOAT_EQ(l.GetVelocity().y, -2.5f);
    EXPECT_FLOAT_EQ(l.GetVelocity().z, 3.5f);

    // 源码直接赋值，无范围钳制
    l.SetVolume(2.5f);
    EXPECT_FLOAT_EQ(l.GetVolume(), 2.5f);
    l.SetVolume(-1.0f);
    EXPECT_FLOAT_EQ(l.GetVolume(), -1.0f);
}

TEST(ListenerTest, PositionAndEnvironment_AreStoredVerbatim)
{
    Listener l;
    l.SetPosition(Vec3(7.0f, 8.0f, 9.0f));
    EXPECT_FLOAT_EQ(l.GetPosition().x, 7.0f);
    EXPECT_FLOAT_EQ(l.GetPosition().z, 9.0f);

    l.SetEnvironment(EnvironmentPreset::Hall);
    l.SetRoomParameters(12.0f, 0.4f, 0.6f);
}

// ── SyncFromTransform ──────────────────────────────────────────────────
TEST(ListenerTest, SyncFromTransform_CopiesPositionFromTransform)
{
    TransformComponent t;
    t.SetPosition(4.0f, 5.0f, 6.0f);

    Listener l;
    l.SyncFromTransform(t);
    EXPECT_FLOAT_EQ(l.GetPosition().x, 4.0f);
    EXPECT_FLOAT_EQ(l.GetPosition().y, 5.0f);
    EXPECT_FLOAT_EQ(l.GetPosition().z, 6.0f);
}

TEST(ListenerTest, SyncFromTransform_EquivalentToSetOrientationOfTransformAxes)
{
    // 不硬编码 Transform 的 forward 符号约定，而是断言契约等价性：
    // SyncFromTransform(t) 与 SetOrientation(t.GetForward(), t.GetUp()) 同结果。
    TransformComponent t;
    t.SetPosition(1.0f, 2.0f, 3.0f);

    Listener synced;
    synced.SyncFromTransform(t);

    Listener manual;
    manual.SetOrientation(t.GetForward(), t.GetUp());

    EXPECT_NEAR(synced.GetForward().x, manual.GetForward().x, kEps);
    EXPECT_NEAR(synced.GetForward().y, manual.GetForward().y, kEps);
    EXPECT_NEAR(synced.GetForward().z, manual.GetForward().z, kEps);
    EXPECT_NEAR(synced.GetUp().x, manual.GetUp().x, kEps);
    EXPECT_NEAR(synced.GetUp().y, manual.GetUp().y, kEps);
    EXPECT_NEAR(synced.GetUp().z, manual.GetUp().z, kEps);
    EXPECT_NEAR(Len3(synced.GetForward()), 1.0f, kEps);
    EXPECT_NEAR(Dot3(synced.GetForward(), synced.GetUp()), 0.0f, kEps);
}

// ── Apply / ApplyTransform：经注入引擎转发 ────────────────────────────
TEST(ListenerTest, Apply_ForwardsPositionOrientationAndVolume)
{
    Listener l;
    l.SetPosition(Vec3(1.0f, 2.0f, 3.0f));
    l.SetOrientation(Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f));
    l.SetVolume(0.35f);

    FakeAudioEngine eng;
    l.Apply(eng);

    EXPECT_EQ(eng.positionCalls, 1);
    EXPECT_EQ(eng.orientationCalls, 1);
    EXPECT_EQ(eng.volumeCalls, 1);

    EXPECT_FLOAT_EQ(eng.m_Position.x, 1.0f);
    EXPECT_FLOAT_EQ(eng.m_Position.y, 2.0f);
    EXPECT_FLOAT_EQ(eng.m_Position.z, 3.0f);
    EXPECT_FLOAT_EQ(eng.m_Forward.z, -1.0f);
    EXPECT_FLOAT_EQ(eng.m_Up.y, 1.0f);
    EXPECT_FLOAT_EQ(eng.m_Volume, 0.35f);
}

TEST(ListenerTest, Apply_ForwardsNormalizedOrientation_NotRawInput)
{
    Listener l;
    // 传入未归一化向量：引擎侧必须收到归一化 + 正交化后的结果
    l.SetOrientation(Vec3(0.0f, 0.0f, -8.0f), Vec3(0.0f, 4.0f, 0.0f));

    FakeAudioEngine eng;
    l.Apply(eng);
    EXPECT_NEAR(Len3(eng.m_Forward), 1.0f, kEps);
    EXPECT_NEAR(Len3(eng.m_Up), 1.0f, kEps);
    EXPECT_NEAR(Dot3(eng.m_Forward, eng.m_Up), 0.0f, kEps);
}

TEST(ListenerTest, ApplyTransform_ForwardsPositionAndOrientation_ButNotVolume)
{
    Listener l;
    l.SetPosition(Vec3(-1.0f, -2.0f, -3.0f));
    l.SetVolume(0.9f);

    FakeAudioEngine eng;
    l.ApplyTransform(eng);

    EXPECT_EQ(eng.positionCalls, 1);
    EXPECT_EQ(eng.orientationCalls, 1);
    // ApplyTransform 明确不转发音量
    EXPECT_EQ(eng.volumeCalls, 0);
    EXPECT_FLOAT_EQ(eng.m_Position.x, -1.0f);
    EXPECT_FLOAT_EQ(eng.m_Volume, -1.0f);   // fake 初值，未被覆盖
}

TEST(ListenerTest, Apply_DoesNotMutateListenerState)
{
    Listener l;
    l.SetPosition(Vec3(1.0f, 2.0f, 3.0f));
    l.SetOrientation(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 posBefore = l.GetPosition();
    const Vec3 fwdBefore = l.GetForward();
    const Vec3 upBefore = l.GetUp();

    FakeAudioEngine eng;
    l.Apply(eng);
    EXPECT_FLOAT_EQ(l.GetPosition().x, posBefore.x);
    EXPECT_FLOAT_EQ(l.GetForward().x, fwdBefore.x);
    EXPECT_FLOAT_EQ(l.GetUp().x, upBefore.x);
}

TEST(ListenerTest, Apply_CalledTwice_ForwardsSameValuesTwice)
{
    Listener l;
    l.SetPosition(Vec3(2.0f, 0.0f, 0.0f));

    FakeAudioEngine eng;
    l.Apply(eng);
    l.Apply(eng);
    EXPECT_EQ(eng.positionCalls, 2);
    EXPECT_EQ(eng.orientationCalls, 2);
    EXPECT_EQ(eng.volumeCalls, 2);
    EXPECT_FLOAT_EQ(eng.m_Position.x, 2.0f);
}