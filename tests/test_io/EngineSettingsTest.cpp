// P1: IO/Config 子系统测试 —— EngineSettings
//
// EngineSettings（82 行头 + 117 行实现）此前零测试。它是 Config 之上的
// 薄封装：三个子结构（Graphics / Audio / Physics）共 15 个字段，
// 每个字段对应 BuildDefaults() 里的一条注册默认值。
//
// 断言集中在"封装层自身"的语义，而不是重复 Config 的测试：
//
//   - 构造后的初始状态，以及"注册默认值"与"实际生效值"的区别
//   - Load 永不失败（缺失/损坏文件时回落到默认并回写）的返回契约
//   - Load 是"合并"而非"替换"：文件里缺失的键保留原值
//   - Save 只序列化"实际生效值"，不序列化注册默认值
//   - RestoreDefaults 把注册模板物化进 m_Data
//   - 各 section 互相独立；无任何数值校验或钳制
//
// 注意 API 实况（读源码确认，未猜测）：
//   - 构造函数只做 BuildDefaults() + m_Config.SetDefaults(m_Defaults)
//   - Config::SetDefaults 仅注册 m_Defaults 并置 m_HasDefaults，
//     **不**把默认值写入 m_Data
//   - 因此 fresh instance 的 m_Data 是空对象，各 getter 全部回落到
//     自带的 defaultValue 参数（其取值与 BuildDefaults 一致）
//   - Config::Load 解析失败时**不修改** m_Data（合并循环不会执行），
//     随后 EngineSettings::Load 调 RestoreDefaults() 覆盖为默认值
//   - Config::GetInt 对 string 值做 std::stoi，故数字字符串可被强转
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>

#include "Engine/Core/EngineSettings.h"

using namespace Engine;

namespace {

class TempSettingsFile {
public:
    explicit TempSettingsFile(const char* tag) {
        const auto dir = std::filesystem::temp_directory_path() / "engine_enginesettings_tests";
        std::filesystem::create_directories(dir);
        m_Path = (dir / (std::string(tag) + ".json")).string();
        std::error_code ec;
        std::filesystem::remove(m_Path, ec);
    }
    ~TempSettingsFile() {
        std::error_code ec;
        std::filesystem::remove(m_Path, ec);
    }
    const std::string& path() const { return m_Path; }
    bool exists() const { return std::filesystem::exists(m_Path); }

    void writeText(const std::string& text) const {
        std::ofstream out(m_Path, std::ios::binary | std::ios::trunc);
        out << text;
    }

    std::string readText() const {
        std::ifstream in(m_Path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

private:
    std::string m_Path;
};

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

// ── 初始状态 ───────────────────────────────────────────────────────────
TEST(EngineSettingsTest, FreshInstance_Graphics_ReportsDocumentedDefaults)
{
    EngineSettings s;
    const auto g = s.GetGraphics();
    EXPECT_EQ(g.windowWidth, 1280);
    EXPECT_EQ(g.windowHeight, 720);
    EXPECT_FALSE(g.fullscreen);
    EXPECT_TRUE(g.vsync);
    EXPECT_EQ(g.targetFPS, 60);
    EXPECT_FLOAT_EQ(g.renderScale, 1.0f);
    EXPECT_FALSE(g.enablePostFX);
    EXPECT_EQ(g.shadowMapSize, 1024);
}

TEST(EngineSettingsTest, FreshInstance_Audio_ReportsDocumentedDefaults)
{
    EngineSettings s;
    const auto a = s.GetAudio();
    EXPECT_FLOAT_EQ(a.masterVolume, 1.0f);
    EXPECT_FLOAT_EQ(a.musicVolume, 0.8f);
    EXPECT_FLOAT_EQ(a.sfxVolume, 1.0f);
    EXPECT_EQ(a.audioChannels, 32);
}

TEST(EngineSettingsTest, FreshInstance_Physics_ReportsDocumentedDefaults)
{
    EngineSettings s;
    const auto p = s.GetPhysics();
    EXPECT_EQ(p.velocityIterations, 8);
    EXPECT_EQ(p.positionIterations, 3);
    EXPECT_FLOAT_EQ(p.gravityScale, 1.0f);
}

TEST(EngineSettingsTest, FreshInstance_RepeatedGets_AreStableAndSideEffectFree)
{
    EngineSettings s;
    const auto g1 = s.GetGraphics();
    const auto a1 = s.GetAudio();
    const auto p1 = s.GetPhysics();
    const auto g2 = s.GetGraphics();
    const auto a2 = s.GetAudio();
    const auto p2 = s.GetPhysics();
    EXPECT_EQ(g1.windowWidth, g2.windowWidth);
    EXPECT_EQ(g1.shadowMapSize, g2.shadowMapSize);
    EXPECT_FLOAT_EQ(a1.musicVolume, a2.musicVolume);
    EXPECT_EQ(p1.velocityIterations, p2.velocityIterations);
}

// ── Set/Get 往返 ───────────────────────────────────────────────────────
TEST(EngineSettingsTest, SetGraphics_ThenGet_ReflectsEveryField)
{
    EngineSettings s;
    EngineSettings::Graphics g;
    g.windowWidth   = 1920;
    g.windowHeight  = 1080;
    g.fullscreen    = true;
    g.vsync         = false;
    g.targetFPS     = 144;
    g.renderScale   = 1.5f;
    g.enablePostFX  = true;
    g.shadowMapSize = 2048;
    s.SetGraphics(g);

    const auto r = s.GetGraphics();
    EXPECT_EQ(r.windowWidth, 1920);
    EXPECT_EQ(r.windowHeight, 1080);
    EXPECT_TRUE(r.fullscreen);
    EXPECT_FALSE(r.vsync);
    EXPECT_EQ(r.targetFPS, 144);
    EXPECT_FLOAT_EQ(r.renderScale, 1.5f);
    EXPECT_TRUE(r.enablePostFX);
    EXPECT_EQ(r.shadowMapSize, 2048);
}

TEST(EngineSettingsTest, SetAudio_ThenGet_ReflectsEveryField)
{
    EngineSettings s;
    EngineSettings::Audio a;
    a.masterVolume  = 0.25f;
    a.musicVolume   = 0.5f;
    a.sfxVolume     = 0.75f;
    a.audioChannels = 64;
    s.SetAudio(a);

    const auto r = s.GetAudio();
    EXPECT_FLOAT_EQ(r.masterVolume, 0.25f);
    EXPECT_FLOAT_EQ(r.musicVolume, 0.5f);
    EXPECT_FLOAT_EQ(r.sfxVolume, 0.75f);
    EXPECT_EQ(r.audioChannels, 64);
}

TEST(EngineSettingsTest, SetPhysics_ThenGet_ReflectsEveryField)
{
    EngineSettings s;
    EngineSettings::Physics p;
    p.velocityIterations = 16;
    p.positionIterations = 7;
    p.gravityScale       = 2.5f;
    s.SetPhysics(p);

    const auto r = s.GetPhysics();
    EXPECT_EQ(r.velocityIterations, 16);
    EXPECT_EQ(r.positionIterations, 7);
    EXPECT_FLOAT_EQ(r.gravityScale, 2.5f);
}

TEST(EngineSettingsTest, SetGet_ExtremeValues_AreStoredVerbatim_NoValidationOrClamping)
{
    EngineSettings s;
    EngineSettings::Graphics g;
    g.windowWidth   = 0;
    g.windowHeight  = -1;
    g.targetFPS     = -5;
    g.shadowMapSize = 999999;
    g.renderScale   = -2.5f;
    s.SetGraphics(g);

    const auto r = s.GetGraphics();
    EXPECT_EQ(r.windowWidth, 0);
    EXPECT_EQ(r.windowHeight, -1);
    EXPECT_EQ(r.targetFPS, -5);
    EXPECT_EQ(r.shadowMapSize, 999999);
    EXPECT_FLOAT_EQ(r.renderScale, -2.5f);
}

TEST(EngineSettingsTest, SetGet_SectionsAreIndependent)
{
    EngineSettings s;
    EngineSettings::Graphics g;
    g.windowWidth = 800;
    s.SetGraphics(g);

    // 写 Graphics 不应影响 Audio / Physics
    EXPECT_EQ(s.GetAudio().audioChannels, 32);
    EXPECT_FLOAT_EQ(s.GetAudio().masterVolume, 1.0f);
    EXPECT_EQ(s.GetPhysics().velocityIterations, 8);

    EngineSettings::Audio a;
    a.masterVolume = 0.1f;
    s.SetAudio(a);

    EXPECT_EQ(s.GetGraphics().windowWidth, 800);   // Graphics 未被 Audio 写影响
    EXPECT_EQ(s.GetPhysics().positionIterations, 3);
}

// ── Save / Load 往返 ───────────────────────────────────────────────────
TEST(EngineSettingsTest, SaveLoad_AllSections_RoundTripThroughFile)
{
    TempSettingsFile f("roundtrip");
    EngineSettings src;
    EngineSettings::Graphics g;
    g.windowWidth   = 1600;
    g.windowHeight  = 900;
    g.fullscreen    = true;
    g.vsync         = false;
    g.targetFPS     = 75;
    g.renderScale   = 0.75f;
    g.enablePostFX  = true;
    g.shadowMapSize = 4096;
    EngineSettings::Audio a;
    a.masterVolume  = 0.35f;
    a.musicVolume   = 0.45f;
    a.sfxVolume     = 0.55f;
    a.audioChannels = 16;
    EngineSettings::Physics p;
    p.velocityIterations = 12;
    p.positionIterations = 5;
    p.gravityScale       = 1.5f;
    src.SetGraphics(g);
    src.SetAudio(a);
    src.SetPhysics(p);
    ASSERT_TRUE(src.Save(f.path()));

    EngineSettings dst;
    ASSERT_TRUE(dst.Load(f.path()));
    const auto rg = dst.GetGraphics();
    EXPECT_EQ(rg.windowWidth, 1600);
    EXPECT_EQ(rg.windowHeight, 900);
    EXPECT_TRUE(rg.fullscreen);
    EXPECT_FALSE(rg.vsync);
    EXPECT_EQ(rg.targetFPS, 75);
    EXPECT_FLOAT_EQ(rg.renderScale, 0.75f);
    EXPECT_TRUE(rg.enablePostFX);
    EXPECT_EQ(rg.shadowMapSize, 4096);
    const auto ra = dst.GetAudio();
    EXPECT_FLOAT_EQ(ra.masterVolume, 0.35f);
    EXPECT_FLOAT_EQ(ra.musicVolume, 0.45f);
    EXPECT_FLOAT_EQ(ra.sfxVolume, 0.55f);
    EXPECT_EQ(ra.audioChannels, 16);
    const auto rp = dst.GetPhysics();
    EXPECT_EQ(rp.velocityIterations, 12);
    EXPECT_EQ(rp.positionIterations, 5);
    EXPECT_FLOAT_EQ(rp.gravityScale, 1.5f);
}

TEST(EngineSettingsTest, Save_SerializesOnlyActiveData_NotRegisteredDefaults)
{
    TempSettingsFile f("active_only");
    EngineSettings s;   // 从未 Set：m_Data 仍是空对象
    ASSERT_TRUE(s.Save(f.path()));
    // 注册默认值不会被写盘 —— 只有实际生效值会
    EXPECT_EQ(Trim(f.readText()), "{}");
}

TEST(EngineSettingsTest, Load_MissingFile_ReturnsTrue_CreatesFile_AndReportsDefaults)
{
    TempSettingsFile f("missing");
    ASSERT_FALSE(f.exists());
    EngineSettings s;
    EXPECT_TRUE(s.Load(f.path()));      // 永不失败
    EXPECT_TRUE(f.exists());            // 但会自建默认文件
    EXPECT_EQ(s.GetGraphics().windowWidth, 1280);
    EXPECT_FLOAT_EQ(s.GetAudio().musicVolume, 0.8f);
    EXPECT_EQ(s.GetPhysics().velocityIterations, 8);
}

TEST(EngineSettingsTest, Load_MissingFile_CreatedFile_IsValidJsonObject)
{
    TempSettingsFile f("missing_valid");
    EngineSettings s;
    ASSERT_TRUE(s.Load(f.path()));
    const std::string text = Trim(f.readText());
    EXPECT_FALSE(text.empty());
    EXPECT_EQ(text.front(), '{');
    EXPECT_EQ(text.back(), '}');
    EXPECT_NE(text.find("Graphics"), std::string::npos);
    EXPECT_NE(text.find("Audio"), std::string::npos);
    EXPECT_NE(text.find("Physics"), std::string::npos);
}

TEST(EngineSettingsTest, Load_MalformedJson_ReturnsTrue_FallsBackToDefaults_AndRewritesFile)
{
    TempSettingsFile f("malformed");
    f.writeText("{ this is not valid json ]]");
    EngineSettings s;
    EngineSettings::Graphics g;
    g.windowWidth = 4242;
    s.SetGraphics(g);                  // 先污染状态

    EXPECT_TRUE(s.Load(f.path()));     // 解析失败也返回 true
    EXPECT_EQ(s.GetGraphics().windowWidth, 1280);   // 被默认值覆盖
    // 且损坏文件被回写为合法默认配置
    const std::string text = Trim(f.readText());
    EXPECT_EQ(text.front(), '{');
    EXPECT_NE(text.find("Graphics"), std::string::npos);
}

TEST(EngineSettingsTest, Load_JsonArrayNotObject_ReturnsTrue_FallsBackToDefaults)
{
    TempSettingsFile f("not_object");
    f.writeText("[1, 2, 3]");
    EngineSettings s;
    EXPECT_TRUE(s.Load(f.path()));
    EXPECT_EQ(s.GetGraphics().windowHeight, 720);
    EXPECT_EQ(s.GetAudio().audioChannels, 32);
    const std::string text = Trim(f.readText());
    EXPECT_EQ(text.front(), '{');      // 数组已被回写成对象
}

TEST(EngineSettingsTest, Load_UnwritablePath_StillReturnsTrue)
{
    EngineSettings s;
    // 目标目录不存在 → Config::Save 失败，但返回值被忽略
    const std::string bad =
        (std::filesystem::temp_directory_path() / "engine_enginesettings_tests" /
         "no_such_dir_xyz" / "settings.json").string();
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::temp_directory_path() /
                                    "engine_enginesettings_tests" / "no_such_dir_xyz",
                                ec);
    EXPECT_TRUE(s.Load(bad));
    // 即便落盘失败，内存状态仍应可用
    EXPECT_EQ(s.GetGraphics().windowWidth, 1280);
}

TEST(EngineSettingsTest, Load_PartialFile_MissingKeysFallBackToDocumentedDefaults)
{
    TempSettingsFile f("partial");
    f.writeText(R"({"Graphics": {"windowWidth": 2560}})");
    EngineSettings s;
    ASSERT_TRUE(s.Load(f.path()));
    const auto g = s.GetGraphics();
    EXPECT_EQ(g.windowWidth, 2560);    // 文件提供
    EXPECT_EQ(g.windowHeight, 720);    // 文件缺失 → 回落默认
    EXPECT_TRUE(g.vsync);
    EXPECT_EQ(g.shadowMapSize, 1024);
    // 完全缺失的 section 也回落默认
    EXPECT_EQ(s.GetAudio().audioChannels, 32);
    EXPECT_EQ(s.GetPhysics().positionIterations, 3);
}

TEST(EngineSettingsTest, Load_MergeGranularity_IsPerSection_NotPerKey)
{
    TempSettingsFile f("merge");
    EngineSettings s;
    EngineSettings::Graphics g;
    g.windowWidth   = 1281;
    g.shadowMapSize = 2048;
    s.SetGraphics(g);
    EngineSettings::Audio a;
    a.audioChannels = 64;
    s.SetAudio(a);
    ASSERT_TRUE(s.Save(f.path()));

    // 文件只带 Graphics section，且其中只有 windowWidth
    f.writeText(R"({"Graphics": {"windowWidth": 1600}})");
    ASSERT_TRUE(s.Load(f.path()));

    const auto r = s.GetGraphics();
    EXPECT_EQ(r.windowWidth, 1600);      // 文件提供的键被采用
    // Config::Load 的合并粒度是 **section**：m_Data[section] = 整个 value，
    // 因此被替换的 section 内缺失的键不会保留旧值，而是回落到默认。
    EXPECT_EQ(r.shadowMapSize, 1024);
    // 文件未出现的 section 完全不受影响，旧值保留
    EXPECT_EQ(s.GetAudio().audioChannels, 64);
}

TEST(EngineSettingsTest, Load_NumericStringValue_IsCoercedByGetters)
{
    TempSettingsFile f("coerce");
    f.writeText(R"({"Graphics": {"windowWidth": "1920"}})");
    EngineSettings s;
    ASSERT_TRUE(s.Load(f.path()));
    // Config::GetInt 对 string 走 std::stoi
    EXPECT_EQ(s.GetGraphics().windowWidth, 1920);
}

// ── RestoreDefaults ─────────────────────────────────────────────────────
TEST(EngineSettingsTest, RestoreDefaults_AfterMutation_ResetsAllSections)
{
    EngineSettings s;
    EngineSettings::Graphics g;
    g.windowWidth = 800;
    g.shadowMapSize = 512;
    EngineSettings::Audio a;
    a.masterVolume = 0.2f;
    EngineSettings::Physics p;
    p.velocityIterations = 99;
    s.SetGraphics(g);
    s.SetAudio(a);
    s.SetPhysics(p);

    s.RestoreDefaults();

    EXPECT_EQ(s.GetGraphics().windowWidth, 1280);
    EXPECT_EQ(s.GetGraphics().shadowMapSize, 1024);
    EXPECT_FLOAT_EQ(s.GetAudio().masterVolume, 1.0f);
    EXPECT_EQ(s.GetPhysics().velocityIterations, 8);
}

TEST(EngineSettingsTest, RestoreDefaults_CalledTwice_IsIdempotent)
{
    EngineSettings s;
    EngineSettings::Graphics g;
    g.windowWidth = 640;
    s.SetGraphics(g);

    s.RestoreDefaults();
    const auto first = s.GetGraphics();
    s.RestoreDefaults();
    const auto second = s.GetGraphics();

    EXPECT_EQ(first.windowWidth, 1280);
    EXPECT_EQ(second.windowWidth, first.windowWidth);
    EXPECT_EQ(second.shadowMapSize, first.shadowMapSize);
}

TEST(EngineSettingsTest, RestoreDefaults_MakesRegisteredDefaultsObservable_AfterMutation)
{
    TempSettingsFile f("restore_then_save");
    EngineSettings s;
    EngineSettings::Graphics g;
    g.windowWidth = 333;
    s.SetGraphics(g);
    s.RestoreDefaults();
    ASSERT_TRUE(s.Save(f.path()));
    // RestoreDefaults 物化后的默认值应真正落盘
    EXPECT_NE(f.readText().find("1280"), std::string::npos);
}

// ── 类型约束 ───────────────────────────────────────────────────────────
TEST(EngineSettingsTest, Type_IsNonCopyable)
{
    static_assert(!std::is_copy_constructible<EngineSettings>::value,
                  "EngineSettings must not be copyable");
    static_assert(!std::is_copy_assignable<EngineSettings>::value,
                  "EngineSettings must not be copy-assignable");
    SUCCEED();
}