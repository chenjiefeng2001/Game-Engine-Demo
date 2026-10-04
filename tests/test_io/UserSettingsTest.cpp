// P1: IO/Config 子系统测试 —— UserSettings
//
// UserSettings（66 行头 + 119 行实现）此前零测试。它是 Config 之上的
// 薄封装，因此断言集中在"封装层自身"的语义，而不是重复 Config 的测试：
//
//   - 构造后的初始状态，以及"注册默认值"与"实际生效值"的区别
//   - Load 永不失败（缺失文件时自建默认并落盘）的返回契约
//   - RestoreDefaults 必须真正物化注册模板中的值
//   - Recent files：去重、最近优先、上限 10、JSON 数组往返
//   - Key bindings：往返、未知 action 返回空串
//
// 注意 API 实况（读源码确认，未猜测）：
//   - UserSettings 构造函数只做 BuildDefaults() + m_Config.SetDefaults(...)
//   - Config::SetDefaults 仅注册 m_Defaults 并置 m_HasDefaults，
//     **不**把默认值写入 m_Data
//   - 因此"已注册默认值"与"当前生效值"是两件事：Get* 的取值来自
//     m_Data，缺失时回落到各 getter 自带的 defaultValue 参数
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Engine/Core/UserSettings.h"

using namespace Engine;

namespace {

class TempSettingsFile {
public:
    explicit TempSettingsFile(const char* tag) {
        const auto dir = std::filesystem::temp_directory_path() / "engine_usersettings_tests";
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

private:
    std::string m_Path;
};

} // namespace

// ── 初始状态 ───────────────────────────────────────────────────────────
TEST(UserSettingsTest, FreshInstanceReportsDocumentedScalarDefaults)
{
    UserSettings s;
    // 这些值来自各 getter 的 defaultValue 参数
    EXPECT_TRUE(s.IsEditorVisible());
    EXPECT_FLOAT_EQ(s.GetUIScale(), 1.0f);
    EXPECT_TRUE(s.IsPerformanceOverlayVisible());
}

TEST(UserSettingsTest, FreshInstanceHasNoRecentFiles)
{
    UserSettings s;
    EXPECT_TRUE(s.GetRecentFiles().empty());
}

TEST(UserSettingsTest, FreshInstanceKeyBindingsAreEmptyUntilDefaultsRestored)
{
    // 关键语义：构造函数只**注册**默认模板，不把它物化进 m_Config。
    // BuildDefaults() 里有 "toggleUI" = "F1"，但 GetKeyBinding 读的是
    // m_Config，缺失时回落到 getter 的 defaultValue（空串）。
    // 因此全新实例取不到 F1，必须先 RestoreDefaults()。
    UserSettings fresh;
    EXPECT_EQ(fresh.GetKeyBinding("toggleUI"), "")
        << "若此断言失败，说明构造函数已开始物化默认模板，请更新本测试";

    UserSettings restored;
    restored.RestoreDefaults();
    EXPECT_EQ(restored.GetKeyBinding("toggleUI"), "F1")
        << "RestoreDefaults 之后应能取到已注册的默认键位";
    EXPECT_EQ(restored.GetKeyBinding("toggleConsole"), "F2");
    EXPECT_EQ(restored.GetKeyBinding("saveScene"), "Ctrl+S");
    EXPECT_EQ(restored.GetKeyBinding("play"), "F5");
    EXPECT_EQ(restored.GetKeyBinding("stop"), "Shift+F5");
}

// ── RestoreDefaults ────────────────────────────────────────────────────
TEST(UserSettingsTest, RestoreDefaultsRevertsEdits)
{
    UserSettings s;
    s.RestoreDefaults();

    s.SetEditorVisible(false);
    s.SetUIScale(2.5f);
    s.SetPerformanceOverlayVisible(false);
    s.SetKeyBinding("toggleUI", "F12");
    EXPECT_FALSE(s.IsEditorVisible());
    EXPECT_FLOAT_EQ(s.GetUIScale(), 2.5f);

    s.RestoreDefaults();
    EXPECT_TRUE(s.IsEditorVisible()) << "RestoreDefaults 未还原 visible";
    EXPECT_FLOAT_EQ(s.GetUIScale(), 1.0f) << "RestoreDefaults 未还原 uiScale";
    EXPECT_TRUE(s.IsPerformanceOverlayVisible());
    EXPECT_EQ(s.GetKeyBinding("toggleUI"), "F1") << "RestoreDefaults 未还原键位";
}

TEST(UserSettingsTest, RestoreDefaultsRevertsRecentFilesToEmpty)
{
    UserSettings s;
    s.RestoreDefaults();
    s.AddRecentFile("a.txt");
    ASSERT_EQ(s.GetRecentFiles().size(), 1u);

    s.RestoreDefaults();
    EXPECT_TRUE(s.GetRecentFiles().empty()) << "RestoreDefaults 未清空 recent files";
}

// ── 标量存取 ───────────────────────────────────────────────────────────
TEST(UserSettingsTest, ScalarSettersRoundTrip)
{
    UserSettings s;
    s.SetEditorVisible(false);
    EXPECT_FALSE(s.IsEditorVisible());
    s.SetEditorVisible(true);
    EXPECT_TRUE(s.IsEditorVisible());

    s.SetUIScale(1.75f);
    EXPECT_FLOAT_EQ(s.GetUIScale(), 1.75f);

    s.SetPerformanceOverlayVisible(false);
    EXPECT_FALSE(s.IsPerformanceOverlayVisible());
}

TEST(UserSettingsTest, KeyBindingRoundTripsAndUnknownActionIsEmpty)
{
    UserSettings s;
    s.SetKeyBinding("custom", "Ctrl+Shift+P");
    EXPECT_EQ(s.GetKeyBinding("custom"), "Ctrl+Shift+P");
    EXPECT_EQ(s.GetKeyBinding("neverSet"), "") << "未知 action 应返回空串";
}

// ── Recent files ───────────────────────────────────────────────────────
TEST(UserSettingsTest, RecentFilesRoundTripPreservesOrder)
{
    UserSettings s;
    const std::vector<std::string> files = {"a.txt", "b.txt", "c.txt"};
    s.SetRecentFiles(files);
    EXPECT_EQ(s.GetRecentFiles(), files) << "recent files 往返丢失或改变了顺序";
}

TEST(UserSettingsTest, RecentFilesHandleEmptyVector)
{
    UserSettings s;
    s.SetRecentFiles({});
    EXPECT_TRUE(s.GetRecentFiles().empty());
    s.SetRecentFiles({"only.txt"});
    ASSERT_EQ(s.GetRecentFiles().size(), 1u);
    s.SetRecentFiles({});
    EXPECT_TRUE(s.GetRecentFiles().empty()) << "清空后仍应为空";
}

TEST(UserSettingsTest, AddRecentFilePutsNewestFirst)
{
    UserSettings s;
    s.AddRecentFile("first.txt");
    s.AddRecentFile("second.txt");
    s.AddRecentFile("third.txt");

    const auto f = s.GetRecentFiles();
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[0], "third.txt") << "最新项应在最前";
    EXPECT_EQ(f[2], "first.txt") << "最早项应在最后";
}

TEST(UserSettingsTest, AddRecentFileDeduplicates)
{
    UserSettings s;
    s.AddRecentFile("dup.txt");
    s.AddRecentFile("other.txt");
    s.AddRecentFile("dup.txt");

    const auto f = s.GetRecentFiles();
    int count = 0;
    for (const auto& x : f) if (x == "dup.txt") ++count;
    EXPECT_EQ(count, 1) << "重复项未被去重";
    EXPECT_EQ(f.front(), "dup.txt") << "重复添加后应置于最前";
}

TEST(UserSettingsTest, AddRecentFileCapsAtTenNewestFirst)
{
    UserSettings s;
    for (int i = 0; i < 15; ++i) {
        s.AddRecentFile("file" + std::to_string(i) + ".txt");
    }
    const auto f = s.GetRecentFiles();
    ASSERT_EQ(f.size(), 10u) << "recent files 未按上限 10 截断";
    // 最后写入的 file14 应在最前，file4 之后应被淘汰
    EXPECT_EQ(f.front(), "file14.txt");
    bool foundOld = false;
    for (const auto& x : f) if (x == "file4.txt") foundOld = true;
    EXPECT_FALSE(foundOld) << "最旧的 5 项应已被淘汰";
}

// ── Load / Save ────────────────────────────────────────────────────────
TEST(UserSettingsTest, LoadOnMissingFileCreatesItAndReturnsTrue)
{
    UserSettings s;
    s.RestoreDefaults();
    s.SetUIScale(1.25f);

    TempSettingsFile f("create_on_load");
    ASSERT_FALSE(f.exists());

    EXPECT_TRUE(s.Load(f.path())) << "Load 对缺失文件应自建默认并返回 true";
    EXPECT_TRUE(f.exists()) << "Load 未落盘";
}

TEST(UserSettingsTest, SaveLoadRoundTripPreservesSettings)
{
    UserSettings original;
    original.RestoreDefaults();
    original.SetEditorVisible(false);
    original.SetUIScale(2.25f);
    original.SetPerformanceOverlayVisible(false);
    original.SetKeyBinding("toggleUI", "F11");
    original.AddRecentFile("x.txt");
    original.AddRecentFile("y.txt");

    TempSettingsFile f("roundtrip");
    ASSERT_TRUE(original.Save(f.path()));

    UserSettings loaded;
    ASSERT_TRUE(loaded.Load(f.path()));
    EXPECT_FALSE(loaded.IsEditorVisible());
    EXPECT_FLOAT_EQ(loaded.GetUIScale(), 2.25f);
    EXPECT_FALSE(loaded.IsPerformanceOverlayVisible());
    EXPECT_EQ(loaded.GetKeyBinding("toggleUI"), "F11");
    EXPECT_EQ(loaded.GetRecentFiles(), (std::vector<std::string>{"y.txt", "x.txt"}));
}

TEST(UserSettingsTest, LoadAlwaysReturnsTrueEvenOnUnwritablePath)
{
    // 契约记录：Load 内部在失败时会 RestoreDefaults + Save，但**无条件**
    // 返回 true。调用方无法通过返回值区分"加载成功"与"新建默认"。
    // 因此未写入路径时 Save 失败，Load 依然报 true。
    UserSettings s;
    const bool ok = s.Load("no/such/directory/deeper/settings.json");
    EXPECT_TRUE(ok)
        << "Load 当前契约为恒返回 true；若改为可失败，此断言需更新并补失败路径覆盖";
}

TEST(UserSettingsTest, SaveToUnwritablePathReportsFailure)
{
    UserSettings s;
    s.RestoreDefaults();
    EXPECT_FALSE(s.Save("no/such/directory/settings.json"))
        << "Save 对不可写路径应返回 false";
}

TEST(UserSettingsTest, LoadFromMalformedJsonFallsBackToDefaults)
{
    TempSettingsFile f("malformed");
    {
        std::ofstream out(f.path(), std::ios::binary | std::ios::trunc);
        out << "{ not json";
    }

    UserSettings s;
    EXPECT_TRUE(s.Load(f.path())) << "Load 契约恒返回 true";
    // 加载失败 -> RestoreDefaults -> 应回到默认标量值
    EXPECT_TRUE(s.IsEditorVisible());
    EXPECT_FLOAT_EQ(s.GetUIScale(), 1.0f);
    EXPECT_EQ(s.GetKeyBinding("toggleUI"), "F1")
        << "加载失败后应回落到默认模板（含键位）";
}

TEST(UserSettingsTest, ReloadOverwritesPreviousState)
{
    TempSettingsFile f("reload");
    {
        UserSettings a;
        a.RestoreDefaults();
        a.SetUIScale(3.0f);
        a.SetKeyBinding("toggleUI", "F9");
        ASSERT_TRUE(a.Save(f.path()));
    }

    UserSettings b;
    ASSERT_TRUE(b.Load(f.path()));
    EXPECT_FLOAT_EQ(b.GetUIScale(), 3.0f);
    EXPECT_EQ(b.GetKeyBinding("toggleUI"), "F9");

    // 覆写为另一份设置后再加载
    {
        UserSettings c;
        c.RestoreDefaults();
        c.SetUIScale(0.5f);
        ASSERT_TRUE(c.Save(f.path()));
    }
    UserSettings d;
    ASSERT_TRUE(d.Load(f.path()));
    EXPECT_FLOAT_EQ(d.GetUIScale(), 0.5f) << "二次加载未覆盖旧值（Config::Load 为合并语义）";
    EXPECT_EQ(d.GetKeyBinding("toggleUI"), "F1")
        << "被覆盖掉的键位应回落默认值";
}

TEST(UserSettingsTest, UserSettingsIsNonCopyable)
{
    static_assert(!std::is_copy_constructible<UserSettings>::value,
                  "UserSettings must not be copyable");
    static_assert(!std::is_copy_assignable<UserSettings>::value,
                  "UserSettings must not be copy-assignable");
    SUCCEED();
}
