// Avalonia 主窗口布局 —— 结构不变量守卫
//
// 动机：MainWindow.axaml 的四区 Shell 用 Grid 布局，但区域位置是**声明式**的
// （Grid.Column / Grid.Row / RowSpan / ColumnSpan），编译期无法发现两个控件
// 抢同一批单元格。Avalonia 对重叠不报错 —— 后声明的控件直接画在上层，
// 于是前面那个控件的下半部分被静默盖住。
//
// 这不是假设。2026-10-18 的布局复核就撞到了实例：视口与 Inspector 都声明
// `Grid.RowSpan="3"`，而底部 TabControl 声明 `Grid.Column="2" Grid.ColumnSpan="3"
// Grid.Row="2"`，于是 TabControl 同时压住：
//   · 视口（Column=2, Row=2）
//   · Inspector（Column=4, Row=2）
// 各 220px 高的一截被完全遮住，没有任何编译错误或警告，UI 也照常启动。
//
// 为什么用 ctest 而不是 xunit：dotnet restore 在当前环境不可用（NuGet 源
// 不可达且本地无包缓存），Avalonia 工程根本编译不了，托管侧一个断言都跑不了。
// 但布局重叠是**纯结构事实**，从 XAML 文本即可判定 —— 所以守卫放在这边。
//
// 覆盖不到的：XAML 是否能被 Avalonia 编译器接受（属性名拼写、绑定路径是否存在
// 等）。那需要 dotnet build，见本文件末尾的说明。

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

namespace {

namespace fs = std::filesystem;

std::string ReadWholeFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

fs::path FindRepoRoot() {
    fs::path dir = fs::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (fs::exists(dir / "editor_avalonia" / "MainWindow.axaml") &&
            fs::exists(dir / "engine" / "src"))
            return dir;
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

// ────────────────────────────────────────────────────────────────────
// 极小的 XAML 标签扫描器
//
// 不引入 XML 库：这个 target 只需要「布局 Grid 的直接子元素 + 它们的
// Grid.* 属性」，而 XAML 在这一点上是高度规则的文本。正则/手写扫描在这里
// 比拉一个 XML 依赖更合适 —— 它不引入新的第三方依赖去验证一份不依赖它也能
// 判断正确性的东西。
// ────────────────────────────────────────────────────────────────────

struct Tag {
    std::string Name;                 // 例如 "Border"
    std::map<std::string, std::string> Attrs;
    bool SelfClosing = false;
    size_t Start = 0;
};

/// 解析一个从 `<` 开始的标签。pos 指向 '<'。解析失败返回 false。
bool ParseTag(const std::string& s, size_t pos, Tag* out) {
    if (pos >= s.size() || s[pos] != '<') return false;
    size_t i = pos + 1;
    if (i < s.size() && (s[i] == '/' || s[i] == '!' || s[i] == '?')) return false;

    size_t nameStart = i;
    while (i < s.size() && !isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' &&
           s[i] != '/')
        ++i;
    out->Name = s.substr(nameStart, i - nameStart);
    out->Attrs.clear();
    out->SelfClosing = false;

    // 属性：Name="value" 或 Name='value'
    while (i < s.size()) {
        while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i >= s.size()) return false;
        if (s[i] == '/') { out->SelfClosing = true; ++i; continue; }
        if (s[i] == '>') return true;

        size_t aStart = i;
        while (i < s.size() && s[i] != '=' && !isspace(static_cast<unsigned char>(s[i]))) ++i;
        std::string attr = s.substr(aStart, i - aStart);
        while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && s[i] == '=') ++i;
        while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
            const char quote = s[i++];
            size_t vStart = i;
            while (i < s.size() && s[i] != quote) ++i;
            out->Attrs[attr] = s.substr(vStart, i - vStart);
            if (i < s.size()) ++i;   // 跳过收尾引号
        } else {
            // 无引号属性值
            size_t vStart = i;
            while (i < s.size() && !isspace(static_cast<unsigned char>(s[i])) &&
                   s[i] != '>' && s[i] != '/')
                ++i;
            out->Attrs[attr] = s.substr(vStart, i - vStart);
        }
    }
    return false;
}

int AttrOr(const Tag& t, const std::string& name, int fallback) {
    auto it = t.Attrs.find(name);
    if (it == t.Attrs.end() || it->second.empty()) return fallback;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

/// 找出「四区 Shell」布局 Grid 的开标签位置：带显式 ColumnDefinitions 的那个 Grid。
size_t FindLayoutGridStart(const std::string& s) {
    const std::string marker = "<Grid.ColumnDefinitions>";
    const size_t defs = s.find(marker);
    if (defs == std::string::npos) return std::string::npos;
    // 必须从 defs 之前找：defs 处的子串本身就以 "<Grid" 开头，
    // rfind("<Grid", defs) 会命中属性元素而不是布局 Grid 本身。
    const size_t grid = s.rfind("<Grid", defs - 1);
    return grid == std::string::npos ? std::string::npos : grid;
}

/// Avalonia 的属性元素（Grid.ColumnDefinitions / Style.Resources 等）名字里带点，
/// 它们不是可视子元素，不参与单元格占用计算。
bool IsPropertyElement(const std::string& name) {
    return name.find('.') != std::string::npos;
}

/// 收集布局 Grid 的**直接子元素**（深度 1）。
std::vector<Tag> DirectChildrenOfLayoutGrid(const std::string& s) {
    std::vector<Tag> out;
    const size_t gridStart = FindLayoutGridStart(s);
    if (gridStart == std::string::npos) return out;

    Tag gridTag;
    if (!ParseTag(s, gridStart, &gridTag)) return out;

    size_t pos = gridStart;
    // 跳过布局 Grid 自身的开标签
    {
        size_t i = pos + 1;
        while (i < s.size() && s[i] != '>') ++i;
        pos = (i < s.size()) ? i + 1 : s.size();
    }

    int depth = 1;
    while (pos < s.size()) {
        const size_t lt = s.find('<', pos);
        if (lt == std::string::npos) break;

        if (s.compare(lt, 2, "</") == 0) {
            --depth;
            if (depth == 0) break;
            const size_t gt = s.find('>', lt);
            if (gt == std::string::npos) break;
            pos = gt + 1;
            continue;
        }

        Tag tag;
        if (!ParseTag(s, lt, &tag)) {
            // 注释 / CDATA / 处理指令
            const size_t gt = s.find('>', lt);
            if (gt == std::string::npos) break;
            pos = gt + 1;
            continue;
        }
        size_t i = lt + 1;
        while (i < s.size() && s[i] != '>') ++i;
        const size_t tagEnd = (i < s.size()) ? i + 1 : s.size();

        if (depth == 1 && !IsPropertyElement(tag.Name)) {
            tag.Start = lt;
            out.push_back(tag);
        }
        if (!tag.SelfClosing) ++depth;
        pos = tagEnd;
    }
    return out;
}

struct CellOwner {
    std::string What;
    int Line = 0;
};

int LineOf(const std::string& s, size_t pos) {
    return static_cast<int>(std::count(s.begin(), s.begin() + pos, '\n')) + 1;
}

std::string Describe(const Tag& t) {
    auto it = t.Attrs.find("x:Name");
    const std::string name = (it != t.Attrs.end()) ? t.Attrs.at("x:Name") : "(未命名)";
    return "<" + t.Name + " " + name + ">";
}

fs::path XamlPath() {
    return FindRepoRoot() / "editor_avalonia" / "MainWindow.axaml";
}

std::string LoadXaml() {
    const fs::path p = XamlPath();
    EXPECT_FALSE(p.empty()) << "从 " << fs::current_path() << " 向上找不到仓库根";
    return ReadWholeFile(p);
}

// ────────────────────────────────────────────────────────────────────
// 核心不变量：布局 Grid 的直接子元素不得抢占同一批单元格
// ────────────────────────────────────────────────────────────────────

TEST(AvaloniaMainWindowLayout, NoTwoRegionsShareTheSameGridCells) {
    const std::string xaml = LoadXaml();
    const std::vector<Tag> children = DirectChildrenOfLayoutGrid(xaml);
    ASSERT_FALSE(children.empty()) << "没解析出布局 Grid 的直接子元素 —— "
                                   << "扫描器可能已与 XAML 结构脱节";

    // cell -> 占用者。默认 Column=0 / Row=0 / Span=1，与 Avalonia 语义一致。
    std::map<std::pair<int, int>, CellOwner> occupied;
    std::vector<std::string> conflicts;

    for (const Tag& t : children) {
        const int col = AttrOr(t, "Grid.Column", 0);
        const int row = AttrOr(t, "Grid.Row", 0);
        const int colSpan = AttrOr(t, "Grid.ColumnSpan", 1);
        const int rowSpan = AttrOr(t, "Grid.RowSpan", 1);

        for (int c = col; c < col + colSpan; ++c) {
            for (int r = row; r < row + rowSpan; ++r) {
                auto key = std::make_pair(c, r);
                auto it = occupied.find(key);
                if (it != occupied.end()) {
                    conflicts.push_back(
                        std::string("单元格 (col=") + std::to_string(c) + ", row=" +
                        std::to_string(r) + ") 同时被 " + it->second.What +
                        " (行 " + std::to_string(it->second.Line) + ") 和 " +
                        Describe(t) + " (行 " + std::to_string(LineOf(xaml, t.Start)) +
                        ") 占用。Avalonia 不报重叠错误，后声明者画在上层，"
                        "被盖住的那块内容直接消失且无任何告警。");
                } else {
                    occupied[key] = CellOwner{Describe(t), LineOf(xaml, t.Start)};
                }
            }
        }
    }

    if (!conflicts.empty()) {
        std::ostringstream msg;
        msg << "主窗口四区布局存在 " << conflicts.size() << " 处单元格重叠：\n";
        for (const std::string& c : conflicts) msg << "  - " << c << "\n";
        FAIL() << msg.str();
    }
}

TEST(AvaloniaMainWindowLayout, TheFourZonesCoverEveryCellExactlyOnce) {
    const std::string xaml = LoadXaml();
    const std::vector<Tag> children = DirectChildrenOfLayoutGrid(xaml);
    ASSERT_FALSE(children.empty()) << "没解析出布局 Grid 的直接子元素";

    // 「四区 Shell」在 5 列 × 3 行上的完整划分。不留空单元格，也不重叠：
    //
    //        col0        col1      col2        col3      col4
    //   row0  Hierarchy   ‖竖      Viewport    ‖竖      Inspector
    //   row1  Hierarchy   ‖竖      ──横分隔条──          (横分隔条)
    //   row2  Hierarchy   ‖竖      MainTabs(跨 col2..col4)
    //
    // 注：Hierarchy 与竖分隔条通高（RowSpan=3）；横分隔条跨 col2..col4。
    struct Expect { int Col; int Row; const char* What; };
    const Expect kExpected[] = {
        {0, 0, "Hierarchy 左区"},      {0, 1, "Hierarchy 左区"},  {0, 2, "Hierarchy 左区"},
        {1, 0, "左分隔条"},            {1, 1, "左分隔条"},        {1, 2, "左分隔条"},
        {2, 0, "Viewport"},            {3, 0, "右分隔条"},        {4, 0, "Inspector"},
        {2, 1, "横分隔条"},            {3, 1, "横分隔条"},        {4, 1, "横分隔条"},
        {2, 2, "MainTabs"},            {3, 2, "MainTabs"},        {4, 2, "MainTabs"},
    };

    // 重算占用表
    std::map<std::pair<int, int>, const Tag*> occupied;
    std::vector<std::string> dupes;
    for (const Tag& t : children) {
        const int col = AttrOr(t, "Grid.Column", 0);
        const int row = AttrOr(t, "Grid.Row", 0);
        const int colSpan = AttrOr(t, "Grid.ColumnSpan", 1);
        const int rowSpan = AttrOr(t, "Grid.RowSpan", 1);
        for (int c = col; c < col + colSpan; ++c) {
            for (int r = row; r < row + rowSpan; ++r) {
                const auto key = std::make_pair(c, r);
                if (occupied.count(key)) {
                    dupes.push_back("单元格 (col=" + std::to_string(c) + ", row=" +
                                    std::to_string(r) + ") 被 " +
                                    Describe(*occupied[key]) + " 与 " + Describe(t) + " 同时占用");
                }
                occupied[key] = &t;
            }
        }
    }

    std::ostringstream msg;
    for (const Expect& e : kExpected) {
        const auto it = occupied.find(std::make_pair(e.Col, e.Row));
        if (it == occupied.end()) {
            msg << "单元格 (col=" << e.Col << ", row=" << e.Row << ") 无人占用，"
                << "但按「四区 Shell」它应属于 " << e.What << "\n";
            continue;
        }
        if (e.Col == 2 && e.Row == 2) {
            // MainTabs 有 x:Name，可直接断言
            auto nm = it->second->Attrs.find("x:Name");
            const std::string actual = (nm != it->second->Attrs.end()) ? nm->second : "(无)";
            if (actual != "MainTabs") {
                msg << "单元格 (2,2) 应由 MainTabs 占用，实际是 " << actual << "\n";
            }
        }
        if (e.Col == 2 && e.Row == 0) {
            // 视口区域：Column=2 且不跨行
            if (AttrOr(*it->second, "Grid.RowSpan", 1) != 1) {
                msg << "视口区 (col=2,row=0) 不应跨行，实际 RowSpan="
                    << AttrOr(*it->second, "Grid.RowSpan", 1)
                    << "（跨行会压住下方分隔条与底部 TabControl）\n";
            }
        }
        if (e.Col == 4 && e.Row == 0) {
            if (AttrOr(*it->second, "Grid.RowSpan", 1) != 1) {
                msg << "Inspector 区 (col=4,row=0) 不应跨行，实际 RowSpan="
                    << AttrOr(*it->second, "Grid.RowSpan", 1)
                    << "（跨行会与底部 TabControl 重叠）\n";
            }
        }
    }
    for (const std::string& d : dupes) msg << "重叠: " << d << "\n";

    EXPECT_TRUE(msg.str().empty()) << msg.str();
}

// ────────────────────────────────────────────────────────────────────
// 分隔条：三个 8px 间隙必须真的可拖动
// ────────────────────────────────────────────────────────────────────

TEST(AvaloniaMainWindowLayout, TheThreeGapsAreDraggableSplitters) {
    const std::string xaml = LoadXaml();
    const std::vector<Tag> children = DirectChildrenOfLayoutGrid(xaml);

    int columnSplitters = 0;
    int rowSplitters = 0;
    for (const Tag& t : children) {
        if (t.Name != "GridSplitter") continue;
        const std::string dir = t.Attrs.count("ResizeDirection")
                                    ? t.Attrs.at("ResizeDirection") : "";
        if (dir == "Columns") ++columnSplitters;
        else if (dir == "Rows") ++rowSplitters;
        else ADD_FAILURE() << "GridSplitter 的 ResizeDirection 缺失或非预期: '" << dir << "'";
    }

    // Column 1 / 3 与 Row 1 这三段间隙若不是可拖动分隔条，
    // 用户就无法调整 Hierarchy / Inspector / 底部面板的尺寸。
    EXPECT_EQ(columnSplitters, 2)
        << "应有 2 个 Columns 分隔条（第 1/3 列间隙），实际 " << columnSplitters;
    EXPECT_EQ(rowSplitters, 1)
        << "应有 1 个 Rows 分隔条（第 1 行间隙），实际 " << rowSplitters;
}

TEST(AvaloniaMainWindowLayout, WindowHasMinimumSize) {
    const std::string xaml = LoadXaml();
    Tag window;
    ASSERT_TRUE(ParseTag(xaml, 0, &window));
    EXPECT_EQ(window.Name, "Window");
    EXPECT_TRUE(window.Attrs.count("MinWidth") != 0)
        << "Window 缺 MinWidth：固定列合计 240+8+8+280=536px，"
        << "没有下限时窗口可被缩到中央区消失";
    EXPECT_TRUE(window.Attrs.count("MinHeight") != 0)
        << "Window 缺 MinHeight：底部行固定 220px";
}

TEST(AvaloniaMainWindowLayout, EveryAxisDefinitionHasAMinimumSize) {
    const std::string xaml = LoadXaml();

    int cols = 0;
    int rows = 0;
    int colsWithoutMin = 0;
    int rowsWithoutMin = 0;

    // 逐个解析 ColumnDefinition / RowDefinition（从标签起点 '<' 开始解析）
    for (const char* kind : {"<ColumnDefinition", "<RowDefinition"}) {
        size_t pos = 0;
        while (true) {
            const size_t at = xaml.find(kind, pos);
            if (at == std::string::npos) break;
            Tag t;
            ASSERT_TRUE(ParseTag(xaml, at, &t)) << "无法解析 " << kind << " 标签";
            if (t.Name == "ColumnDefinition") {
                ++cols;
                // 分隔条列固定 8px，不需要也不应该有 MinWidth；
                // 承载内容的列必须有下限，否则窗口一缩小该区就被压没。
                const std::string w = t.Attrs.count("Width") ? t.Attrs.at("Width") : "";
                if (w != "8" && !t.Attrs.count("MinWidth")) ++colsWithoutMin;
            } else {
                ++rows;
                const std::string h = t.Attrs.count("Height") ? t.Attrs.at("Height") : "";
                if (h != "8" && !t.Attrs.count("MinHeight")) ++rowsWithoutMin;
            }
            pos = at + 1;
        }
    }

    ASSERT_EQ(cols, 5) << "期望 5 个 ColumnDefinitions（240|8|*|8|280），实际 " << cols;
    ASSERT_EQ(rows, 3) << "期望 3 个 RowDefinitions（*|8|220），实际 " << rows;
    EXPECT_EQ(colsWithoutMin, 0) << "有 " << colsWithoutMin
                                 << " 个承载内容的列缺少 MinWidth（8px 分隔条列除外）";
    EXPECT_EQ(rowsWithoutMin, 0) << "有 " << rowsWithoutMin
                                 << " 个承载内容的行缺少 MinHeight（8px 分隔条行除外）";
}

TEST(AvaloniaMainWindowLayout, ViewportImageIsNotStretchedNone) {
    const std::string xaml = LoadXaml();
    const size_t name = xaml.find("ViewportImage");
    ASSERT_NE(name, std::string::npos) << "XAML 里找不到 ViewportImage";

    // find() 命中的是 x:Name="..." 里的值位置，回退到该标签的 '<' 才能解析属性。
    const size_t tagStart = xaml.rfind('<', name);
    ASSERT_NE(tagStart, std::string::npos);
    Tag t;
    ASSERT_TRUE(ParseTag(xaml, tagStart, &t));
    const std::string stretch = t.Attrs.count("Stretch") ? t.Attrs.at("Stretch") : "(未设置)";

    // 当前呈现尺寸固定 800x600（D4 resize 仍 Deferred），Stretch=None 会
    // 在控件小于图像时裁切一角、大于图像时贴左上角留白。
    EXPECT_NE(stretch, "None")
        << "ViewportImage Stretch=\"None\"：视口会被裁切或在左上角留白，"
        << "应使用 Uniform 等比缩放";
    EXPECT_TRUE(stretch == "Uniform" || stretch == "UniformToFill")
        << "ViewportImage 的 Stretch 应为 Uniform 系列，实际 '" << stretch << "'";
}

// ────────────────────────────────────────────────────────────────────
// 跨文件接线：XAML 绑定 ↔ ViewModel 成员
//
// 本仓库反复出现的失效模式是「两端都有，中间没有」：
//   · GizmoSpace 的 ABI 与 ViewModel 属性都存在，工具栏却没有控件
//   · ViewportCamX/Y/Z 三个字段声明后从未被引用，没有任何可绑定的面
//   · SetViewportCameraPosition / GetViewportCameraPosition 成对存在且从不被调用
// 这些都不会编译失败、不会运行报错，只是功能缺席。因此这里断言
// 「XAML 里绑定的成员在 ViewModel 里确实存在」。
// ────────────────────────────────────────────────────────────────────

std::string LoadViewModel() {
    const fs::path p = FindRepoRoot() / "editor_avalonia" / "ViewModels" / "MainViewModel.cs";
    EXPECT_FALSE(p.empty()) << "从 " << fs::current_path() << " 向上找不到仓库根";
    return ReadWholeFile(p);
}

std::string LoadCodeBehind() {
    const fs::path p = FindRepoRoot() / "editor_avalonia" / "MainWindow.axaml.cs";
    return ReadWholeFile(p);
}

bool ViewModelDeclares(const std::string& vm, const std::string& member) {
    // public <type> Member  或  private <type> _member
    const std::string pattern =
        "(public|private|protected)[^;{]*[ \\t]" + member + "\\b";
    return std::regex_search(vm, std::regex(pattern));
}

TEST(AvaloniaMainWindowLayout, BoundViewportAndGizmoMembersExistOnTheViewModel) {
    const std::string xaml = LoadXaml();
    const std::string vm = LoadViewModel();
    ASSERT_FALSE(vm.empty()) << "读不到 MainViewModel.cs";

    // 这些是视口/Gizmo/编辑器相机的可绑定面。缺任何一个都意味着
    // 「UI 看着有、实际推不到引擎」或「ABI 有、UI 无处设置」。
    const char* kMembers[] = {
        "GizmoMode",        "GizmoSpace",      "GizmoSpaceLocal",
        "GizmoSnapEnabled", "GizmoSnapValue",  "ViewportGridVisible",
        "ViewportCamX",     "ViewportCamY",    "ViewportCamZ",
        "ViewportProjection", "ViewportZoom",  "ViewportStatusText",
        "SetViewportSize",  "UpdateViewportStatus",
    };
    for (const char* m : kMembers) {
        EXPECT_TRUE(ViewModelDeclares(vm, m))
            << "MainViewModel 上不存在 " << m
            << " —— XAML/调用点绑定或调用了它，绑定会静默失败（得到默认值）";
    }
}

TEST(AvaloniaMainWindowLayout, GizmoSpaceAndSnapAreEditableFromTheToolbar) {
    const std::string xaml = LoadXaml();

    // 7-C 的 ABI（SetGizmoSpace / SetGizmoSnap）此前无 UI 入口。
    EXPECT_NE(xaml.find("GizmoSpaceLocal"), std::string::npos)
        << "Gizmo 工具栏没有坐标空间控件，GizmoSpace 无从设置";
    EXPECT_NE(xaml.find("GizmoSnapEnabled"), std::string::npos)
        << "Gizmo 工具栏没有吸附开关控件，GizmoSnapEnabled 无从设置";
    EXPECT_NE(xaml.find("GizmoSnapValue"), std::string::npos)
        << "Gizmo 工具栏没有吸附步长输入，GizmoSnapValue 无从设置";
}

TEST(AvaloniaMainWindowLayout, ViewportReportsItsSizeToTheEngine) {
    const std::string xaml = LoadXaml();
    const std::string code = LoadCodeBehind();

    // Gate 7-A「窗口 resize 时 FBO 自动重建」：控件尺寸必须转发给引擎，
    // 否则相机宽高比会停在打开工程那一刻，拖动分隔条后视口被拉伸。
    //
    // 订阅刻意写在代码后置而不是 XAML 的 SizeChanged="..."：写 XAML 处理器
    // 就必须写出事件参数的类型名，而 Avalonia 11 的 SizeChangedEventArgs
    // 所在命名空间并非 Avalonia（首次 CI 运行 CS0234 证伪）。lambda 订阅让
    // 编译器自行推断参数类型。断言因此检查订阅点，而不是 XAML 属性。
    EXPECT_EQ(xaml.find("SizeChanged="), std::string::npos)
        << "XAML 又出现了 SizeChanged=\"...\" 处理器：它要求手写事件参数类型名，"
        << "而该类型名曾被写错过（CI CS0234）。请用代码后置的 lambda 订阅。";
    EXPECT_NE(code.find("ViewportBorder.SizeChanged"), std::string::npos)
        << "代码后置没有订阅 ViewportBorder.SizeChanged，视口尺寸变化不会上报引擎";
    EXPECT_NE(code.find("ReportViewportSize"), std::string::npos)
        << "缺少 ReportViewportSize()";
    EXPECT_NE(code.find("SetViewportSize"), std::string::npos)
        << "ReportViewportSize 没有把尺寸转发给引擎（Vm.SetViewportSize）";
}

TEST(AvaloniaMainWindowLayout, StatusOverlayIsBoundRatherThanHardcoded) {
    const std::string xaml = LoadXaml();

    // 左下角叠加层此前是硬编码的静态 "Viewport" 文本 —— 一个不随任何东西
    // 变化的标签。它必须绑定 ViewportStatusText 才有信息量。
    const size_t at = xaml.find("ViewportStatusText");
    ASSERT_NE(at, std::string::npos)
        << "视口叠加信息没有绑定 ViewportStatusText（是否退回成静态文本？）";
    EXPECT_NE(xaml.rfind("Text=\"", at), std::string::npos)
        << "ViewportStatusText 出现在非 Text 属性里，绑定可能写错了位置";
}

// ────────────────────────────────────────────────────────────────────
// Q/W/E/R 工具快捷键
//
// ImGui 宿主已有（ViewportPanel.cpp:392-401），Avalonia 宿主此前没有 ——
// OnViewportKeyDown 只把 (int)e.Key 转发给引擎，而引擎的快捷键处理在
// OnImGui 内，生产路径根本不执行。语义必须与 ImGui 端一致，且整块被
// !isFlying 包住（飞行中按 W 会同时移动相机并切工具，OBS-T1 冲突）。
// ────────────────────────────────────────────────────────────────────

TEST(AvaloniaMainWindowLayout, ToolShortcutsMirrorTheImGuiHost) {
    const std::string code = LoadCodeBehind();
    ASSERT_FALSE(code.empty()) << "读不到 MainWindow.axaml.cs";

    // 四个键 → 四个模式，且与 ABI 的编号一致（0/1/2/3，见 capi.h:308）
    EXPECT_NE(code.find("GizmoModeSelect = 0"), std::string::npos);
    EXPECT_NE(code.find("GizmoModeTranslate = 1"), std::string::npos);
    EXPECT_NE(code.find("GizmoModeRotate = 2"), std::string::npos);
    EXPECT_NE(code.find("GizmoModeScale = 3"), std::string::npos);

    for (const char* k : {"Key.Q", "Key.W", "Key.E", "Key.R"}) {
        EXPECT_NE(code.find(std::string("case ") + k), std::string::npos)
            << "工具快捷键缺少 " << k << "（ImGui 宿主有，Avalonia 侧不应缺失）";
    }

    // 飞行态守卫：右键按下时不切工具（与 ImGui 的 !isFlying 同义）
    EXPECT_NE(code.find("if (_rightMouseDown) return false;"), std::string::npos)
        << "工具快捷键缺少飞行态守卫 —— 飞行中按 W 会同时移动相机并切工具"
        << "（GP1-DX 审计 OBS-T1），两端必须一致";

    // 修饰键不触发（比 ImGui 更严，是有意偏离）
    EXPECT_NE(code.find("modifiers != KeyModifiers.None"), std::string::npos)
        << "工具快捷键没有屏蔽 Ctrl/Alt/Meta/Shift —— Ctrl+W、Alt+E 这类组合"
        << "会误切工具";

    // 文本输入优先：焦点在输入框时不得切工具
    EXPECT_NE(code.find("IsTextInputFocused"), std::string::npos)
        << "缺少文本焦点判断 —— 在 Script Editor / Inspector 输入框里打字会"
        << "同时切换工具";
    const size_t textGuard = code.find("if (IsTextInputFocused())");
    const size_t shortcut = code.find("TryApplyToolShortcut(e.Key");
    ASSERT_NE(textGuard, std::string::npos);
    ASSERT_NE(shortcut, std::string::npos);
    EXPECT_LT(textGuard, shortcut)
        << "文本焦点判断必须排在快捷键之前，否则护栏形同虚设";

    // 不得按类型名直接引用 PasswordBox。CI 以 CS0246 证伪过：Avalonia 11.2.7 的
    // Avalonia.Controls 下没有 PasswordBox（密码框是 TextBox.PasswordChar），
    // 直接引用会编译失败。必须沿基类链匹配才能同时覆盖两者。
    EXPECT_EQ(code.find("focused is PasswordBox"), std::string::npos)
        << "直接引用 PasswordBox —— 该类型在 Avalonia 11.2.7 下不存在（CS0246）";
    EXPECT_NE(code.find("TextBoxBase"), std::string::npos)
        << "文本焦点判断没有覆盖 TextBox/PasswordBox 的共同基类 —— "
        << "密码框聚焦时会切工具";
}

TEST(AvaloniaMainWindowLayout, MouseReleaseUsesInitialPressButton) {
    const std::string code = LoadCodeBehind();
    ASSERT_FALSE(code.empty());

    // 释放事件到达时按钮已经抬起，用 GetCurrentPoint().Properties 判断会得到
    // -1，引擎永远收不到「按钮已释放」→ rightDown 永久为真 → 相机卡在飞行态，
    // 且 !isFlying 守卫会永久禁用 Q/W/E/R。必须用 InitialPressMouseButton。
    EXPECT_NE(code.find("ToAbiButton(e.InitialPressMouseButton)"), std::string::npos)
        << "鼠标释放未使用 InitialPressMouseButton —— 按钮释放事件丢失，"
        << "飞行态无法退出";

    // 释放处理器内不得再用 Properties 的 Is*Pressed 判断按钮
    const size_t release = code.find("OnViewportPointerReleased");
    ASSERT_NE(release, std::string::npos);
    const size_t next = code.find("private void OnViewportPointerMoved", release);
    const std::string body = code.substr(release, next - release);
    EXPECT_EQ(body.find("IsRightButtonPressed ? 1"), std::string::npos)
        << "释放处理器仍在用 Properties.Is*Pressed 判断按钮 —— 该值在释放时恒为 false";
}

}  // namespace
