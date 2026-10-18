#pragma once

/**
 * @file EditorLayoutSpec.h
 * @brief 引擎编辑器规范停靠布局的**单一事实源**
 *
 * 为什么要有这个头文件：
 * `ApplyDefaultLayout` 原先是一段写在 `EngineEditor::OnImGui()` 内部的 lambda，
 * 窗口名→停靠节点的映射是硬编码字符串。由此产生过两类问题：
 *
 *   1. 漏登记。`SceneViewerPanel` / `SceneManagerPanel` 的 `m_Visible` 默认 true，
 *      但两个标题都不在映射名单里，且它们连 `SetNextWindowPos` 都没有 ——
 *      于是默认启动就产生两个**位置不可控的漂浮 OS 窗**。这正是
 *      docs/GP1-DX-UI-Audit.md §INV-2「布局撕裂」未被真正消除的原因：
 *      引导机制本身是好的，只是名单不全。
 *
 *   2. 无法校验。新增一个默认可见窗口若忘记登记，不会编译失败、不会运行失败，
 *      只是静默退化成一个漂浮窗。静默退化意味着它能长期存活而无人发现。
 *
 * 因此把「有哪些窗口、归哪个区、各区占多大」从 lambda 里提出来，成为可被
 * tests/test_editor_layout 静态校验的数据。
 *
 * 区域语义（注意是**对剩余节点**依次切分，不是对全区分摊）：
 *   Bottom   ← 从 dockspace 底部切 BottomFraction
 *   Right    ← 从剩余区域右侧切 RightFraction
 *   Left     ← 再从剩余区域左侧切 LeftFraction
 *   Center   ← 余量（Viewport）
 *   RightTop ← Right 区上 (1 - RightBottomFraction)，Inspector
 *   RightBottom ← Right 区下 RightBottomFraction，GP01 三件套 Tab 组
 */

#include <cstddef>
#include <cstdint>

namespace Engine {

/// 规范布局中的停靠区域。Zone 数量必须与 kEditorZoneCount 一致。
enum class EditorDockZone : uint8_t {
    Left = 0,
    Center,
    Bottom,
    RightTop,
    RightBottom,
};

inline constexpr int kEditorZoneCount = 5;

/// 一条「窗口标题 → 停靠区域」登记。
struct EditorDockEntry {
    const char* WindowTitle;
    EditorDockZone Zone;
};

// ── 各区占工作区的比例 ──
// 注意 Left/Right 是**依次切分剩余节点**，因此实际占比不等于这里写的值：
//   Bottom        = BottomFraction                       （首次切分，作用于全区）
//   Right         = RightFraction                        （作用于去掉底部后的剩余）
//   Left          = LeftFraction × (1 - RightFraction)   （再作用于两次切分后的剩余）
//   Center        = (1 - RightFraction) × (1 - LeftFraction) × (1 - BottomFraction)
// 推导见 test_editor_layout 的 DerivedWholeAreaFractions 断言。
inline constexpr float kLayoutBottomFraction      = 0.25f;
inline constexpr float kLayoutRightFraction       = 0.24f;
inline constexpr float kLayoutLeftFraction        = 0.22f;
inline constexpr float kLayoutRightBottomFraction = 0.45f;

// ── 窗口登记表 ──
// 规则：**任何默认可见的编辑器窗口都必须在此登记**，否则会以漂浮 OS 窗出现。
// 沙箱专用窗口（sandbox/src/EditorDemo/ 的测试面板）不在此表，它们各自用
// SetNextWindowPos 取得确定位置，不参与产品规范布局。
inline constexpr EditorDockEntry kEditorDockEntries[] = {
    {"Hierarchy",          EditorDockZone::Left},

    // 2026-10-18 补登：两者 m_Visible 默认 true，此前缺失导致默认启动
    // 出现两个位置不可控的漂浮 OS 窗（INV-2 残留）。
    {"Scene Viewer",       EditorDockZone::Left},
    {"Scene Manager",      EditorDockZone::Left},

    {"Viewport",           EditorDockZone::Center},

    {"Console",            EditorDockZone::Bottom},
    {"Performance",        EditorDockZone::Bottom},

    {"Inspector",          EditorDockZone::RightTop},

    {"GP01 Production",    EditorDockZone::RightBottom},
    {"Content (Registry)", EditorDockZone::RightBottom},
    {"Script Editor",      EditorDockZone::RightBottom},
};

inline constexpr size_t kEditorDockEntryCount =
    sizeof(kEditorDockEntries) / sizeof(kEditorDockEntries[0]);

/// 查找某个窗口标题的登记；未登记返回 nullptr。
/// 供 ApplyDefaultLayout 与测试共用，避免两份查找逻辑。
inline constexpr const EditorDockEntry* FindEditorDockEntry(const char* title) {
    for (size_t i = 0; i < kEditorDockEntryCount; ++i) {
        const char* a = kEditorDockEntries[i].WindowTitle;
        const char* b = title;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return &kEditorDockEntries[i];
    }
    return nullptr;
}

}  // namespace Engine
