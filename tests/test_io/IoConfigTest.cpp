// P1: IO/Config 子系统测试 —— Config
//
// 审计结论：Config（141 行头 + 207 行实现）此前**零直接测试**
// （tests/ 下 0 处 ConfigManager/EngineSettings/UserSettings 引用）。
// 它是纯逻辑 + nlohmann/json，无设备依赖，且契约密集：
//
//   - 类型化 getter 的缺省值与类型强转（int/float/bool/string 互转）
//   - Save → Load 的 JSON 往返保真
//   - Load 必须拒绝：不存在文件 / 非 object 顶层 / 畸形 JSON
//   - Load 是**合并**语义，不清空已有数据
//   - SetDefaults / RestoreDefaults / HasDefaults 三者一致
//   - GetDiff 只返回与默认值不同的键；无默认值时返回空
//   - HasKey / HasSection / GetSectionNames / GetKeyNames 查询一致性
//   - Clear 只清数据不清默认模板
//
// 注意 API 实况（读源码确认，未猜测）：
//   - Config 不可拷贝（delete copy），可移动；GetDiff() 按值返回 Config
//   - GetInt：float→截断取整；bool→1/0；string→std::stoi
//   - GetBool：string 仅认 "true"/"1"/"yes"（大小写敏感）
//   - GetString：非字符串走 val.dump()，故 GetString(int 42) == "42"
//   - 所有 getter 用 try/catch 包裹，越界与类型异常一律回落默认值
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>

#include "Engine/Core/Config.h"

using namespace Engine;

namespace {

// 每个用例独享一个临时文件，避免相互干扰。
class TempFile {
public:
    explicit TempFile(const char* tag) {
        const auto dir = std::filesystem::temp_directory_path() / "engine_config_tests";
        std::filesystem::create_directories(dir);
        m_Path = (dir / (std::string(tag) + ".json")).string();
        std::filesystem::remove(m_Path);
    }
    ~TempFile() { std::error_code ec; std::filesystem::remove(m_Path, ec); }

    const std::string& path() const { return m_Path; }

    void writeText(const std::string& text) const {
        std::ofstream f(m_Path, std::ios::binary | std::ios::trunc);
        f << text;
    }

private:
    std::string m_Path;
};

} // namespace

// ── 基本读写往返 ───────────────────────────────────────────────────────
TEST(ConfigTest, StartsEmptyWithNoSections)
{
    Config c;
    EXPECT_TRUE(c.GetSectionNames().empty());
    EXPECT_FALSE(c.HasSection("anything"));
    EXPECT_FALSE(c.HasDefaults());
}

TEST(ConfigTest, SetGetRoundTripsAllFourTypes)
{
    Config c;
    c.SetInt("S", "i", -17);
    c.SetFloat("S", "f", 2.5f);
    c.SetBool("S", "b", true);
    c.SetString("S", "s", "hello");

    EXPECT_EQ(c.GetInt("S", "i", 0), -17);
    EXPECT_FLOAT_EQ(c.GetFloat("S", "f", 0.0f), 2.5f);
    EXPECT_TRUE(c.GetBool("S", "b", false));
    EXPECT_EQ(c.GetString("S", "s", ""), "hello");
}

TEST(ConfigTest, MissingSectionOrKeyYieldsDefault)
{
    Config c;
    c.SetInt("S", "present", 1);

    EXPECT_EQ(c.GetInt("nosuch", "k", 42), 42) << "缺失 section 未回落默认值";
    EXPECT_EQ(c.GetInt("S", "nosuch", 7), 7) << "缺失 key 未回落默认值";
    EXPECT_FLOAT_EQ(c.GetFloat("nope", "k", 1.5f), 1.5f);
    EXPECT_TRUE(c.GetBool("nope", "k", true));
    EXPECT_EQ(c.GetString("nope", "k", "dflt"), "dflt");
}

TEST(ConfigTest, OverwriteReplacesPreviousValue)
{
    Config c;
    c.SetInt("S", "k", 1);
    c.SetInt("S", "k", 2);
    EXPECT_EQ(c.GetInt("S", "k", 0), 2);
    // 覆盖不得产生重复键
    EXPECT_EQ(c.GetKeyNames("S").size(), 1u);
}

// ── 类型强转 ───────────────────────────────────────────────────────────
TEST(ConfigTest, IntAcceptsFloatBoolAndNumericString)
{
    Config c;
    c.SetFloat("S", "fromFloat", 3.9f);
    c.SetBool("S", "fromBool", true);
    c.SetString("S", "fromStr", "123");

    EXPECT_EQ(c.GetInt("S", "fromFloat", 0), 3) << "float→int 应截断";
    EXPECT_EQ(c.GetInt("S", "fromBool", 0), 1) << "bool→int 应为 1";
    EXPECT_EQ(c.GetInt("S", "fromStr", 0), 123) << "数字字符串→int";
}

TEST(ConfigTest, IntRejectsNonNumericString)
{
    Config c;
    c.SetString("S", "word", "not-a-number");
    // std::stoi 抛异常 → 被 catch 吞掉 → 回落默认值
    EXPECT_EQ(c.GetInt("S", "word", -5), -5);
}

TEST(ConfigTest, FloatAcceptsIntAndNumericString)
{
    Config c;
    c.SetInt("S", "i", 7);
    c.SetString("S", "str", "2.5");
    EXPECT_FLOAT_EQ(c.GetFloat("S", "i", 0.0f), 7.0f);
    EXPECT_FLOAT_EQ(c.GetFloat("S", "str", 0.0f), 2.5f);
}

TEST(ConfigTest, BoolCoercionRules)
{
    Config c;
    c.SetInt("S", "zero", 0);
    c.SetInt("S", "nonzero", 5);
    c.SetString("S", "trueStr", "true");
    c.SetString("S", "oneStr", "1");
    c.SetString("S", "yesStr", "yes");
    c.SetString("S", "noStr", "no");
    c.SetString("S", "TrueStr", "True");     // 大写 T 不被识别

    EXPECT_FALSE(c.GetBool("S", "zero", true));
    EXPECT_TRUE(c.GetBool("S", "nonzero", false));
    EXPECT_TRUE(c.GetBool("S", "trueStr", false));
    EXPECT_TRUE(c.GetBool("S", "oneStr", false));
    EXPECT_TRUE(c.GetBool("S", "yesStr", false));
    // 实测：is_string 分支里的 return 是无条件的 —— 任何不在
    // {"true","1","yes"} 里的字符串一律得到 false，**不会**回落到默认值。
    EXPECT_FALSE(c.GetBool("S", "noStr", true)) << "\"no\" 应判为 false";
    EXPECT_FALSE(c.GetBool("S", "TrueStr", true))
        << "字符串布尔判定大小写敏感，\"True\" 不被识别";
}

TEST(ConfigTest, UnrecognizedStringFallbackIsInconsistentAcrossTypedGetters)
{
    // 观察（待产品决策，非缺陷）：面对"存在但不可解释"的值，各 getter 的
    // 回落策略不一致：
    //   GetInt ("abc")  → std::stoi 抛异常 → 被 catch → 回落默认值
    //   GetBool("abc")  → 直接 return false，**不**回落默认值
    // 这可以有两种合理解读（"不可解释的字符串一律 falsey" vs
    // "不可解释即缺省"），现有 API 无法判定作者意图，故只记录实测行为。
    Config c;
    c.SetString("S", "word", "abc");

    EXPECT_EQ(c.GetInt("S", "word", 42), 42) << "GetInt 回落默认值";
    EXPECT_FALSE(c.GetBool("S", "word", true)) << "GetBool 不回落，返回 false";
    EXPECT_EQ(c.GetString("S", "word", "dflt"), "abc")
        << "GetString 对字符串直接返回原值";
}

TEST(ConfigTest, StringOfNonStringUsesJsonDump)
{
    Config c;
    c.SetInt("S", "i", 42);
    c.SetBool("S", "b", true);
    EXPECT_EQ(c.GetString("S", "i", ""), "42") << "非字符串走 dump()";
    EXPECT_EQ(c.GetString("S", "b", ""), "true");
}

// ── 查询 ───────────────────────────────────────────────────────────────
TEST(ConfigTest, HasKeyAndHasSectionAgreeWithStoredData)
{
    Config c;
    c.SetInt("Alpha", "one", 1);

    EXPECT_TRUE(c.HasSection("Alpha"));
    EXPECT_TRUE(c.HasKey("Alpha", "one"));
    EXPECT_FALSE(c.HasKey("Alpha", "two"));
    EXPECT_FALSE(c.HasKey("Beta", "one"));
}

TEST(ConfigTest, SectionNamesReflectAllStoredSections)
{
    Config c;
    c.SetInt("A", "k", 1);
    c.SetInt("B", "k", 2);
    c.SetInt("C", "k", 3);

    auto names = c.GetSectionNames();
    EXPECT_EQ(names.size(), 3u);
    for (const char* want : {"A", "B", "C"}) {
        EXPECT_NE(std::find(names.begin(), names.end(), want), names.end())
            << "缺少 section " << want;
    }
}

TEST(ConfigTest, KeyNamesOfUnknownSectionIsEmpty)
{
    Config c;
    c.SetInt("A", "k1", 1);
    c.SetInt("A", "k2", 2);

    EXPECT_EQ(c.GetKeyNames("A").size(), 2u);
    EXPECT_TRUE(c.GetKeyNames("nosuch").empty()) << "未知 section 应返回空列表";
}

TEST(ConfigTest, SettingSameKeyInTwoSectionsKeepsThemIndependent)
{
    Config c;
    c.SetInt("A", "k", 1);
    c.SetInt("B", "k", 2);
    EXPECT_EQ(c.GetInt("A", "k", 0), 1);
    EXPECT_EQ(c.GetInt("B", "k", 0), 2);
}

// ── Clear ──────────────────────────────────────────────────────────────
TEST(ConfigTest, ClearRemovesAllSections)
{
    Config c;
    c.SetInt("A", "k", 1);
    c.SetInt("B", "k", 2);
    c.Clear();
    EXPECT_TRUE(c.GetSectionNames().empty());
    EXPECT_FALSE(c.HasSection("A"));
}

TEST(ConfigTest, ClearKeepsDefaultsRegistered)
{
    Config c;
    Config defaults;
    defaults.SetInt("A", "k", 99);
    c.SetDefaults(defaults);
    ASSERT_TRUE(c.HasDefaults());

    c.SetInt("A", "k", 1);
    c.Clear();

    // Clear 只重置 m_Data，不动 m_Defaults / m_HasDefaults
    EXPECT_TRUE(c.HasDefaults()) << "Clear 不应注销默认模板";
    c.RestoreDefaults();
    EXPECT_EQ(c.GetInt("A", "k", 0), 99) << "Clear 后仍应能恢复默认值";
}

// ── 默认模板 ───────────────────────────────────────────────────────────
TEST(ConfigTest, RestoreDefaultsWithoutDefaultsIsNoOp)
{
    Config c;
    c.SetInt("A", "k", 5);
    c.RestoreDefaults();               // 未注册默认值，必须安全
    EXPECT_EQ(c.GetInt("A", "k", 0), 5) << "无默认值时 Restore 不应清空数据";
}

TEST(ConfigTest, RestoreDefaultsRevertsUserEdits)
{
    Config defaults;
    defaults.SetInt("A", "k", 10);
    defaults.SetString("A", "s", "orig");

    Config c;
    c.SetDefaults(defaults);
    c.SetInt("A", "k", 999);
    c.SetString("A", "s", "edited");
    EXPECT_EQ(c.GetInt("A", "k", 0), 999);

    c.RestoreDefaults();
    EXPECT_EQ(c.GetInt("A", "k", 0), 10) << "RestoreDefaults 未还原 int";
    EXPECT_EQ(c.GetString("A", "s", ""), "orig") << "RestoreDefaults 未还原 string";
}

TEST(ConfigTest, RestoreDefaultsDiscardsKeysNotInTemplate)
{
    Config defaults;
    defaults.SetInt("A", "k", 1);

    Config c;
    c.SetDefaults(defaults);
    c.SetInt("A", "extra", 123);
    ASSERT_TRUE(c.HasKey("A", "extra"));

    c.RestoreDefaults();
    EXPECT_FALSE(c.HasKey("A", "extra"))
        << "RestoreDefaults 应丢弃模板中不存在的键";
}

TEST(ConfigTest, SetDefaultsDeepCopiesTemplate)
{
    Config defaults;
    defaults.SetInt("A", "k", 1);

    Config c;
    c.SetDefaults(defaults);

    // 改动 defaults 不应影响已注册的模板（深拷贝）
    defaults.SetInt("A", "k", 2);
    c.RestoreDefaults();
    EXPECT_EQ(c.GetInt("A", "k", 0), 1) << "SetDefaults 未深拷贝";
}

// ── GetDiff ────────────────────────────────────────────────────────────
TEST(ConfigTest, GetDiffIsEmptyWithoutDefaults)
{
    Config c;
    c.SetInt("A", "k", 5);
    Config diff = c.GetDiff();
    EXPECT_TRUE(diff.GetSectionNames().empty())
        << "未注册默认值时 GetDiff 应为空";
}

TEST(ConfigTest, GetDiffIsEmptyWhenAllValuesMatchDefaults)
{
    Config defaults;
    defaults.SetInt("A", "k", 1);
    defaults.SetString("A", "s", "x");

    Config c;
    c.SetDefaults(defaults);
    c.SetInt("A", "k", 1);
    c.SetString("A", "s", "x");

    Config diff = c.GetDiff();
    EXPECT_TRUE(diff.GetSectionNames().empty())
        << "与默认值完全一致时 GetDiff 应为空";
}

TEST(ConfigTest, GetDiffReportsOnlyChangedKeys)
{
    Config defaults;
    defaults.SetInt("A", "same", 1);
    defaults.SetInt("A", "changed", 1);
    defaults.SetInt("B", "untouched", 5);

    Config c;
    c.SetDefaults(defaults);
    c.SetInt("A", "same", 1);          // 未改
    c.SetInt("A", "changed", 42);      // 已改
    // B 段完全不写入

    Config diff = c.GetDiff();
    ASSERT_EQ(diff.GetSectionNames().size(), 1u) << "diff 只应包含有改动的 section";
    EXPECT_TRUE(diff.HasSection("A"));
    EXPECT_FALSE(diff.HasSection("B")) << "未改动的 section 不应出现在 diff 中";
    EXPECT_TRUE(diff.HasKey("A", "changed"));
    EXPECT_FALSE(diff.HasKey("A", "same")) << "未改动的键不应出现在 diff 中";
    EXPECT_EQ(diff.GetInt("A", "changed", 0), 42) << "diff 应携带改后的值";
}

TEST(ConfigTest, GetDiffReportsKeysAbsentFromDefaults)
{
    Config defaults;
    defaults.SetInt("A", "known", 1);

    Config c;
    c.SetDefaults(defaults);
    c.SetInt("A", "brandNew", 7);

    Config diff = c.GetDiff();
    EXPECT_TRUE(diff.HasKey("A", "brandNew"))
        << "模板中不存在的键应被视为改动并出现在 diff 中";
}

// ── Save / Load 往返 ───────────────────────────────────────────────────
TEST(ConfigTest, SaveLoadRoundTripPreservesValues)
{
    Config original;
    original.SetInt("Video", "width", 1920);
    original.SetFloat("Video", "gamma", 2.2f);
    original.SetBool("Audio", "enabled", true);
    original.SetString("Game", "title", "Demo");

    TempFile f("roundtrip");
    ASSERT_TRUE(original.Save(f.path())) << "Save 失败";

    Config loaded;
    ASSERT_TRUE(loaded.Load(f.path())) << "Load 失败";
    EXPECT_EQ(loaded.GetInt("Video", "width", 0), 1920);
    EXPECT_FLOAT_EQ(loaded.GetFloat("Video", "gamma", 0.0f), 2.2f);
    EXPECT_TRUE(loaded.GetBool("Audio", "enabled", false));
    EXPECT_EQ(loaded.GetString("Game", "title", ""), "Demo");
}

TEST(ConfigTest, LoadMergesRatherThanReplaces)
{
    Config c;
    c.SetInt("Keep", "mine", 1);

    TempFile f("merge");
    Config other;
    other.SetInt("Added", "theirs", 2);
    ASSERT_TRUE(other.Save(f.path()));

    ASSERT_TRUE(c.Load(f.path()));
    EXPECT_EQ(c.GetInt("Keep", "mine", 0), 1) << "Load 清空了已有数据";
    EXPECT_EQ(c.GetInt("Added", "theirs", 0), 2) << "Load 未合并新数据";
}

TEST(ConfigTest, LoadOverwritesCollidingKeys)
{
    Config c;
    c.SetInt("S", "k", 1);

    TempFile f("collide");
    Config other;
    other.SetInt("S", "k", 99);
    ASSERT_TRUE(other.Save(f.path()));

    ASSERT_TRUE(c.Load(f.path()));
    EXPECT_EQ(c.GetInt("S", "k", 0), 99) << "同名键应被加载值覆盖";
}

TEST(ConfigTest, LoadRejectsMissingFile)
{
    Config c;
    EXPECT_FALSE(c.Load("this/file/does/not/exist.json"));
}

TEST(ConfigTest, LoadRejectsNonObjectTopLevel)
{
    // 顶层是数组：解析成功但不是 object，必须拒绝
    TempFile arr("array");
    arr.writeText("[1, 2, 3]");
    Config c;
    EXPECT_FALSE(c.Load(arr.path())) << "顶层数组应被拒绝";

    TempFile str("stringtop");
    str.writeText("\"just a string\"");
    Config c2;
    EXPECT_FALSE(c2.Load(str.path())) << "顶层字符串应被拒绝";
}

TEST(ConfigTest, LoadRejectsMalformedJson)
{
    TempFile f("malformed");
    f.writeText("{ this is not json ");
    Config c;
    EXPECT_FALSE(c.Load(f.path())) << "畸形 JSON 应被拒绝且不崩溃";
    // 失败后不应留下半截数据
    EXPECT_TRUE(c.GetSectionNames().empty());
}

TEST(ConfigTest, SaveFailsGracefullyOnUnwritablePath)
{
    Config c;
    c.SetInt("A", "k", 1);
    EXPECT_FALSE(c.Save("no/such/directory/out.json"))
        << "不可写路径应返回 false 而非崩溃";
}

TEST(ConfigTest, SaveEmptyConfigProducesLoadableObject)
{
    Config empty;
    TempFile f("emptycfg");
    ASSERT_TRUE(empty.Save(f.path()));

    Config loaded;
    EXPECT_TRUE(loaded.Load(f.path()));
    EXPECT_TRUE(loaded.GetSectionNames().empty());
}

TEST(ConfigTest, SavedFileIsValidUtf8Json)
{
    Config c;
    c.SetString("S", "unicode", "值✓");
    TempFile f("utf8");
    ASSERT_TRUE(c.Save(f.path()));

    std::ifstream in(f.path(), std::ios::binary);
    ASSERT_TRUE(in.is_open());
    const std::string text((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("\"S\""), std::string::npos);
    EXPECT_NE(text.find("unicode"), std::string::npos);
}

// ── ConfigGet 模板特化 ────────────────────────────────────────────────
TEST(ConfigTest, ConfigGetDispatchesToTypedGetters)
{
    Config c;
    c.SetInt("S", "i", 3);
    c.SetFloat("S", "f", 1.5f);
    c.SetBool("S", "b", true);
    c.SetString("S", "s", "v");

    EXPECT_EQ(ConfigGet<int32>(c, "S", "i", 0), 3);
    EXPECT_FLOAT_EQ(ConfigGet<float32>(c, "S", "f", 0.0f), 1.5f);
    EXPECT_TRUE(ConfigGet<bool>(c, "S", "b", false));
    EXPECT_EQ(ConfigGet<std::string>(c, "S", "s", ""), "v");
}

TEST(ConfigTest, ConfigGetFallsBackToSuppliedDefault)
{
    Config c;
    EXPECT_EQ(ConfigGet<int32>(c, "nope", "k", 77), 77);
    EXPECT_EQ(ConfigGet<std::string>(c, "nope", "k", "d"), "d");
}

// ── 类型契约 ───────────────────────────────────────────────────────────
TEST(ConfigTest, ConfigIsNonCopyableButMovable)
{
    static_assert(!std::is_copy_constructible<Config>::value,
                  "Config must not be copyable");
    static_assert(!std::is_copy_assignable<Config>::value,
                  "Config must not be copy-assignable");
    static_assert(std::is_move_constructible<Config>::value,
                  "Config must be movable (GetDiff returns by value)");
    SUCCEED();
}

TEST(ConfigTest, GetRawReflectsStoredShape)
{
    Config c;
    c.SetInt("S", "k", 1);
    const auto& raw = c.GetRaw();
    ASSERT_TRUE(raw.is_object());
    EXPECT_TRUE(raw.contains("S"));
    EXPECT_TRUE(raw["S"].contains("k"));
}
