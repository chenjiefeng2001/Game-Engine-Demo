// 引擎编辑器规范停靠布局 —— 登记完整性守卫
//
// 动机：docs/GP1-DX-UI-Audit.md §INV-2 记录了「布局撕裂」症状（默认启动被拆成
// 十几个漂浮 OS 窗口）。引导机制本身是好的（EngineEditor.cpp 里
// View>Reset Layout → ApplyDefaultLayout → DockBuilder 全链路已通），但
// **窗口名→停靠节点的映射是一段 lambda 里的硬编码字符串**，于是漏登记既不会
// 编译失败、也不会运行失败，只是静默退化成一个漂浮窗。
//
// 静默退化是这类缺陷能长期存活的原因：`SceneViewerPanel` 与 `SceneManagerPanel`
// 的 m_Visible 默认都是 true（SceneViewerPanel.h:186 / SceneManagerPanel.h:74），
// 两者都不在原名单里，且连 SetNextWindowPos 都没有 —— 默认启动就有两个位置
// 不可控的漂浮 OS 窗，而测试全绿。
//
// 因此本 target 的核心不是「测布局长什么样」，而是**让漏登记变成失败**：
//   1. 扫 engine/src 源码，抽出所有带关闭按钮的顶层 ImGui::Begin 窗口；
//   2. 定位其可见性成员的默认值；
//   3. 默认可见者必须在 EditorLayoutSpec.h 的 kEditorDockEntries 里登记，
//      否则列出并失败。
//
// 这不是循环论证：窗口清单来自源码扫描，不是来自登记表。登记表漏了一个窗口，
// 这里就会发现。
//
// 刻意**不**创建 ImGui context、不建窗口、不跑真实 DockBuilder：那样断言的是
// ImGui 的行为而不是本仓库的行为。真正「OS 窗口数从 4 降到 1」只能靠实机
// EnumWindows 取证，本 target 覆盖不到那一层。

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Engine/Editor/EditorLayoutSpec.h"

namespace {

namespace fs = std::filesystem;
using Engine::EditorDockZone;
using Engine::kEditorDockEntries;
using Engine::kEditorDockEntryCount;

// ────────────────────────────────────────────────────────────────────
// 登记表自身的不变量
// ────────────────────────────────────────────────────────────────────

TEST(EditorLayoutSpec, HasNoDuplicateWindowTitles) {
    std::set<std::string> seen;
    for (size_t i = 0; i < kEditorDockEntryCount; ++i) {
        const std::string title = kEditorDockEntries[i].WindowTitle;
        ASSERT_FALSE(title.empty()) << "第 " << i << " 条登记的窗口标题为空";
        EXPECT_TRUE(seen.insert(title).second)
            << "窗口 \"" << title << "\" 重复登记 —— DockBuilderDockWindow "
            << "会对同名窗口重复调用，第二次不会产生额外效果，但通常意味着"
            << "有人改名后忘了删旧条目";
    }
}

TEST(EditorLayoutSpec, EveryZoneValueIndexesTheNodeTable) {
    for (size_t i = 0; i < kEditorDockEntryCount; ++i) {
        const int zone = static_cast<int>(kEditorDockEntries[i].Zone);
        EXPECT_GE(zone, 0) << kEditorDockEntries[i].WindowTitle;
        EXPECT_LT(zone, Engine::kEditorZoneCount) << kEditorDockEntries[i].WindowTitle;
    }
}

TEST(EditorLayoutSpec, FindEntryMatchesExactTitleOnly) {
    for (size_t i = 0; i < kEditorDockEntryCount; ++i) {
        const char* title = kEditorDockEntries[i].WindowTitle;
        const Engine::EditorDockEntry* found = Engine::FindEditorDockEntry(title);
        ASSERT_NE(found, nullptr) << title;
        EXPECT_EQ(found->Zone, kEditorDockEntries[i].Zone) << title;
    }
    // 前缀相同但不是同一个窗口，不得被误配
    EXPECT_EQ(Engine::FindEditorDockEntry("Scene"), nullptr);
    EXPECT_EQ(Engine::FindEditorDockEntry("Viewport "), nullptr);
    EXPECT_EQ(Engine::FindEditorDockEntry("viewport"), nullptr);
    EXPECT_EQ(Engine::FindEditorDockEntry(""), nullptr);
}

// ────────────────────────────────────────────────────────────────────
// 比例：把「对剩余节点依次切分」换算成对整屏工作区的实际占比并钉住
//
// 审计 §8.1 RST-1 写的是「左 Hierarchy 22%」，那是**切分参数**而不是实际占比：
// Left 是在扣掉 Bottom、再扣掉 Right 之后的剩余节点上取的 22%，换算到整屏
// 实际是 0.76 × 0.22 = 16.72%。这里把换算结果钉成断言，避免再次被误读。
// ────────────────────────────────────────────────────────────────────

TEST(EditorLayoutSpec, DerivedWholeAreaFractionsMatchIntendedProportions) {
    // 切分是依次作用于**剩余节点**的。注意 Down 只改高度、不改宽度，
    // 因此宽度链路上不存在底部比例这一环 —— 这是本用例最初写错的地方。
    const float widthAfterRight = 1.0f - Engine::kLayoutRightFraction;
    const float leftWhole = widthAfterRight * Engine::kLayoutLeftFraction;
    const float centerWhole = widthAfterRight * (1.0f - Engine::kLayoutLeftFraction);

    EXPECT_NEAR(Engine::kLayoutBottomFraction, 0.25f, 1e-5f);
    EXPECT_NEAR(Engine::kLayoutRightFraction, 0.24f, 1e-5f);
    EXPECT_NEAR(Engine::kLayoutLeftFraction, 0.22f, 1e-5f);
    EXPECT_NEAR(Engine::kLayoutRightBottomFraction, 0.45f, 1e-5f);

    // 整屏实际占比
    EXPECT_NEAR(leftWhole, 0.1672f, 1e-4f);    // ≠ 0.22，勿再写成 22%
    EXPECT_NEAR(centerWhole, 0.5928f, 1e-4f);
    EXPECT_NEAR(leftWhole + Engine::kLayoutRightFraction + centerWhole, 1.0f, 1e-4f);

    // 右列内部的上下分：Bottom 取 0.45，Top 为余量 0.55
    EXPECT_NEAR(1.0f - Engine::kLayoutRightBottomFraction, 0.55f, 1e-5f);
}

// ────────────────────────────────────────────────────────────────────
// 源码扫描：漏登记即失败
// ────────────────────────────────────────────────────────────────────

struct ScannedWindow {
    std::string Title;
    std::string VisibilityMember;   // Begin 第二实参的 &成员；无则为空
    std::string EnclosingClass;     // 最近的 Class::Method( 反推得到
    std::string SourceFile;
    int Line = 0;
};

std::string ReadWholeFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int CountLineAt(const std::string& text, size_t bytePos) {
    return static_cast<int>(std::count(text.begin(), text.begin() + bytePos, '\n')) + 1;
}

/// 扫 engine/src 下所有 .cpp，找出所有 `ImGui::Begin("标题"` 形式的**顶层窗口**。
///
/// 正则要求 `ImGui::Begin(` 紧跟左括号，因此 BeginChild / BeginPopupModal /
/// BeginViewportSideBar / BeginMenuBar / BeginMainMenuBar / BeginTooltip 全部
/// 天然不匹配 —— 它们不是可停靠的顶层窗口。
///
/// 第二实参若为 `&成员`（带关闭按钮、可由用户开关）则记录下来，用于查默认可见性。
std::vector<ScannedWindow> ScanTopLevelWindows(const fs::path& engineSrc) {
    // 自定义分隔符 RE：内容里含 `)"`，用默认分隔符会提前终止 raw string。
    static const std::regex beginRe(
        R"RE(ImGui::Begin\(\s*"([^"]+)"(\s*,\s*&([A-Za-z_][A-Za-z0-9_]*))?)RE");
    static const std::regex scopeRe(
        R"RE(([A-Za-z_][A-Za-z0-9_]*)\s*::\s*[A-Za-z_][A-Za-z0-9_]*\s*\()RE");

    std::vector<ScannedWindow> found;
    if (!fs::exists(engineSrc)) return found;

    for (const auto& entry : fs::recursive_directory_iterator(engineSrc)) {
        if (!entry.is_regular_file()) continue;
        // .h 也要扫：GP01 的三个宿主级窗口（GP01 Production / Content (Registry)
        // / Script Editor）的 Begin 在 sandbox/src/EditorDemo/GP01ProductionSession.h
        // 里，只扫 .cpp 会把它们误判成「登记表里的陈旧条目」。
        const fs::path ext = entry.path().extension();
        if (ext != ".cpp" && ext != ".h" && ext != ".hpp") continue;

        const std::string text = ReadWholeFile(entry.path());
        for (std::sregex_iterator it(text.begin(), text.end(), beginRe), end; it != end; ++it) {
            const size_t offset = static_cast<size_t>(it->prefix().length());

            ScannedWindow w;
            w.Title = (*it)[1].str();
            w.VisibilityMember = (*it)[3].matched ? (*it)[3].str() : "";
            w.SourceFile =
                fs::relative(entry.path(), engineSrc.parent_path().parent_path())
                    .generic_string();
            w.Line = CountLineAt(text, offset);

            // 从匹配处往前找最近的 Class::Method( 作为所属类。
            const std::string before = text.substr(0, offset);
            std::sregex_iterator rit(before.begin(), before.end(), scopeRe);
            if (rit != std::sregex_iterator())
                w.EnclosingClass = (*rit)[1].str();

            found.push_back(w);
        }
    }
    return found;
}

/// 在**所属类的头文件内**查 `<成员> = true/false;` 的初始化默认值。
///
/// 刻意不在全部头文件里全局搜成员名：`m_Visible` 这种名字在几十个面板里都有，
/// 全局搜取第一个匹配会把 A 面板的窗口判成 B 面板的可见性 —— 本 target 第一版
/// 就是这样产出了 15 条假阳性。
bool LookupDefaultVisibilityInClass(const fs::path& engineInclude,
                                    const std::string& className,
                                    const std::string& member, bool* out) {
    if (className.empty() || member.empty()) return false;

    const fs::path header = engineInclude / "Engine" / (className + ".h");
    std::vector<fs::path> candidates{header};
    if (!fs::exists(candidates.front())) {
        candidates.clear();
        for (const auto& e : fs::recursive_directory_iterator(engineInclude)) {
            if (e.is_regular_file() && e.path().stem().string() == className) {
                candidates.push_back(e.path());
                break;
            }
        }
    }

    static const std::regex declRe(
        R"RE(\b([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(true|false)\s*;)RE");

    for (const fs::path& h : candidates) {
        if (!fs::exists(h)) continue;
        const std::string text = ReadWholeFile(h);

        // 只在 `class <ClassName>` 之后的类体范围内查找。
        static const std::regex classRe(R"(class\s+)RE" + className + R"(\b)");
        std::smatch cm;
        if (!std::regex_search(text, cm, classRe)) continue;

        const std::string body = text.substr(static_cast<size_t>(cm.position()));
        for (std::sregex_iterator it(body.begin(), body.end(), declRe), e; it != e; ++it) {
            if ((*it)[2].str() != member) continue;
            *out = ((*it)[3].str() == "true");
            return true;
        }
    }
    return false;
}

/// 默认可见、但**故意不进入产品规范停靠布局**的窗口。
/// 每条都要写明理由 —— 这张表是「已知例外」，不是「待办清单」。
const std::map<std::string, std::string>& LayoutExemptions() {
    static const std::map<std::string, std::string> kExempt = {};
    return kExempt;
}

/// 收集 engine/src 与 sandbox/src 里所有顶层 ImGui::Begin 的窗口标题。
std::set<std::string> ScanAllWindowTitles(const fs::path& repoRoot) {
    std::set<std::string> titles;
    for (const char* sub : {"engine/src", "sandbox/src"}) {
        const fs::path root = repoRoot / sub;
        for (const ScannedWindow& w : ScanTopLevelWindows(root))
            titles.insert(w.Title);
    }
    return titles;
}

// ── 方向一：登记表 → 源码。查**陈旧条目**。
//
// 这是可靠方向。DockBuilderDockWindow 对不存在的标题是**静默 no-op**，所以
// 面板改名后忘记改登记表，会和漏登记一样静默失效。原始注释「窗口名必须与
// 各面板 Begin() 的实际标题一致」正是在防这件事，但没有校验兜底。
/// 从当前目录向上寻找仓库根（同时含 engine/src 与 tests）。
///
/// 为什么要找而不是直接用 current_path()：本 target 读源码树，而 ctest 的默认
/// 工作目录是 build/tests —— 那里当然没有 engine/src。CMakeLists 里已显式设置
/// WORKING_DIRECTORY，但那份设置极易丢失（新增 target 时漏写、或被放在设置
/// 循环之前注册的测试拿不到）。向上探测让「工作目录不对」表现为一条清晰
/// 的失败信息，而不是一堆「扫描为空」的假失败。
fs::path FindRepoRoot() {
    fs::path dir = fs::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (fs::exists(dir / "engine" / "src") && fs::exists(dir / "tests"))
            return dir;
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

TEST(EditorLayoutSpec, EveryDockedWindowTitleExistsInSource) {
    const fs::path repoRoot = FindRepoRoot();
    ASSERT_FALSE(repoRoot.empty())
        << "从 " << fs::current_path()
        << " 向上找不到仓库根（需同时存在 engine/src 与 tests）。"
        << "本 target 依赖 CTest 的 WORKING_DIRECTORY 指向仓库根。";

    const std::set<std::string> titles = ScanAllWindowTitles(repoRoot);
    ASSERT_FALSE(titles.empty()) << "扫描为空：正则很可能已与代码脱节，"
                                 << "此时本 target 会假绿";

    std::vector<std::string> missing;
    for (size_t i = 0; i < kEditorDockEntryCount; ++i) {
        const std::string title = kEditorDockEntries[i].WindowTitle;
        if (titles.count(title) == 0) missing.push_back(title);
    }
    if (!missing.empty()) {
        std::ostringstream msg;
        msg << "规范布局登记了以下在源码中找不到的窗口标题：\n";
        for (const std::string& m : missing) msg << "  - \"" << m << "\"\n";
        msg << "\n面板改名后忘记同步 kEditorDockEntries 的典型后果："
               "DockBuilderDockWindow 对不存在的标题是静默 no-op，该窗口会以"
               "漂浮 OS 窗出现且没有任何错误。";
        FAIL() << msg.str();
    }
}

// ── 方向二：启动可见面板 → 登记表。查**漏登记**。
//
// 判据不靠启发式，而是取自代码里的权威来源：`EngineEditor::Init` 在
// 初始化时写入的 `m_Visibility.<field> = true`。那几个字段就是「EditorDemo
// 启动时应当出现在屏幕上的面板」。这是比「扫描所有 Begin 再猜默认值」可靠得
// 多的锚点 —— 后者第一版产出了 15 条假阳性（m_Visible 在几十个面板里同名，
// 全局搜成员名会张冠李戴）。
//
// m_Visibility 字段 → 窗口标题的对应关系在下方显式列出。之所以人工列出而不
// 去解析：窗口标题是字符串字面量，没有编译期关联；这张表只有十余行、且任何
// 新增都必须在这里显式登记，因此它本身是可审阅的。
const std::map<std::string, std::string>& StartupVisibilityFieldToWindow() {
    static const std::map<std::string, std::string> kMap = {
        {"sceneHierarchy",   "Hierarchy"},
        {"inspector",        "Inspector"},
        {"console",          "Console"},
        {"performance",      "Performance"},
        {"viewport",         "Viewport"},
        {"sceneManager",     "Scene Manager"},
        {"sceneViewerPanel", "Scene Viewer"},
        // 以下在 Init 里被显式置 false（内容/资产浏览器、依赖图、Renderer
        // Debug、Scene Panel 合并视图），按需从 View 菜单打开，故不在启动集内：
        //   contentBrowser / assetBrowser / depGraph / rendererDebug / scenePanelTabbed
    };
    return kMap;
}

/// 解析 EngineEditor.cpp 里 `m_Visibility.<field> = true;` 的字段集合。
std::set<std::string> ParseStartupVisibleFields(const fs::path& engineEditorCpp) {
    static const std::regex re(
        R"RE(m_Visibility\.([A-Za-z_][A-Za-z0-9_]*)\s*=\s*true\s*;)RE");
    std::set<std::string> fields;
    const std::string text = ReadWholeFile(engineEditorCpp);
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
        fields.insert((*it)[1].str());
    return fields;
}

TEST(EditorLayoutSpec, StartupVisiblePanelsAreAllDocked) {
    const fs::path repoRoot = FindRepoRoot();
    ASSERT_FALSE(repoRoot.empty())
        << "从 " << fs::current_path() << " 向上找不到仓库根";
    const fs::path engineEditorCpp = repoRoot / "engine" / "src" / "Editor" / "EngineEditor.cpp";
    ASSERT_TRUE(fs::exists(engineEditorCpp))
        << "找不到 " << engineEditorCpp << " —— 本用例依赖 CTest 把工作目录设为仓库根";

    const std::set<std::string> fields = ParseStartupVisibleFields(engineEditorCpp);
    ASSERT_FALSE(fields.empty()) << "未解析到任何 m_Visibility.* = true —— "
                                 << "正则已与代码脱节";

    const auto& titleOf = StartupVisibilityFieldToWindow();

    std::vector<std::string> offenders;
    for (const std::string& field : fields) {
        auto it = titleOf.find(field);
        if (it == titleOf.end()) {
            offenders.push_back("m_Visibility." + field +
                                " 初始为 true，但测试没有它对应的窗口标题 —— "
                                "请补进 StartupVisibilityFieldToWindow()，"
                                "否则这条断言等于没检查它");
            continue;
        }
        if (Engine::FindEditorDockEntry(it->second.c_str()) != nullptr) continue;
        offenders.push_back("\"" + it->second + "\"（m_Visibility." + field +
                            " 启动为 true，却未登记进 kEditorDockEntries）");
    }

    if (!offenders.empty()) {
        std::ostringstream msg;
        msg << "以下面板在 EditorDemo 启动时可见，却未进入规范停靠布局，"
               "会以**位置不可控的漂浮 OS 窗**出现（GP1-DX-UI-Audit §INV-2 症状）：\n";
        for (const std::string& o : offenders) msg << "  - " << o << "\n";
        FAIL() << msg.str();
    }
}

TEST(EditorLayoutSpec, StartupVisibleFieldMapCoversEveryTrueField) {
    // 上一个用例会把「测试里没登记的字段」报成 offender；这个用例确保那张
    // 映射表没有悄悄落后于代码。
    const fs::path repoRoot = FindRepoRoot();
    ASSERT_FALSE(repoRoot.empty()) << "从 " << fs::current_path() << " 向上找不到仓库根";
    const fs::path engineEditorCpp = repoRoot / "engine" / "src" / "Editor" / "EngineEditor.cpp";
    const std::set<std::string> fields = ParseStartupVisibleFields(engineEditorCpp);
    const auto& titleOf = StartupVisibilityFieldToWindow();

    std::vector<std::string> unmapped;
    for (const std::string& f : fields) {
        if (titleOf.count(f) == 0) unmapped.push_back(f);
    }
    std::ostringstream msg;
    msg << "这些 m_Visibility 字段启动为 true 但未在测试里映射窗口标题: ";
    for (const std::string& f : unmapped) msg << f << " ";
    EXPECT_TRUE(unmapped.empty()) << msg.str();
}

}  // namespace
