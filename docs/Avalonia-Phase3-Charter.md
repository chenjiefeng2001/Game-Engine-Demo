# Avalonia Editor Migration — Phase 3 Charter (Production Panels)

> 建立日期：2026-08-28 · 分支：avalonia · 依据：`docs/Avalonia-Editor-Migration-v1.md`
> 前置：Phase 2 FROZEN（tag `avalonia-phase2-av-g2`，Closure `docs/Avalonia-Phase2-Closure.md`）

---

## 1. 阶段目标

Phase 3 的目标**不是**"把剩下几个窗口搬过来"，而是：

> **让 Avalonia 编辑器承担一条完整的日常内容生产流程。**

用户能在 Avalonia 编辑器内完成：打开工程 → 创建/重命名/删除实体 →
分配贴图 → 修改 Transform → 打开并修改 game.lua → 保存 →
（Play 观察 Console）→ 保存工程 → 关闭 → 冷启动全恢复。

## 2. 范围（P3-A … P3-F）

| 子项 | 内容 | 验证闭环 |
|------|------|---------|
| P3-A | **Hierarchy 正式版**：Search / Entity list / Selected / Create / Rename / Delete / selection state | Create→Select→Rename→Delete→Save→Reload |
| P3-B | **Inspector 正式版**：Entity(Name) / Transform(XYZ) / Sprite(Asset) 可扩展架构 | Component→ViewModel→View；不实现反射 Inspector |
| P3-C | **Asset Browser**：Search→Select asset→Select entity→Assign→Inspector 更新→Save→Reload | Discovery/Selection 与 ContentRegistry Identity 分离 |
| P3-D | **Script Editor 生产级**：文件名 / Dirty / Save / Reload / Lua 错误显示 | 不做 IDE（无 syntax highlight/autocomplete/debugger/LSP 需求证据） |
| P3-E | **Console**：Engine LOG→Session→Event→Avalonia Console 链路验证 | level filter / search / clear / command input 视证据再谈 |
| P3-F | **AV-G3 Production Authoring Gate**：完整生产循环 | 见 §4 |

**P3-A 优先**：Hierarchy 是所有其他面板的 selection/context 上游。
先稳定它，Inspector / Asset Browser / Script Editor 围绕同一个
`SelectedEntity` 契约接入，避免面板间隐式耦合。

## 3. 架构分界

```text
Avalonia
├── Hierarchy       ← Phase 3
├── Inspector       ← Phase 3
├── Asset Browser   ← Phase 3
├── Script Editor   ← Phase 3
├── Console         ← Phase 3
└── Viewport        ← Phase 7（隔离，不碰）
```

**Viewport 红线**：C++ GPU → FBO/Texture → GPU interop → Avalonia rendering
是"渲染架构迁移"量级，不是 UI 迁移。Phase 3 保持占位（AV-003 DECIDED）。

**架构纪律**：

```text
Asset Browser = Discovery / Selection
ContentRegistry = Identity（GUID 管理绝不回 UI）
Inspector = Configuration
```

## 4. AV-G3 Production Authoring Gate

完整生产循环（Phase 3 收尾验收）：

```text
Cold Start
  ↓ Open Project
  ↓ Create Entity
  ↓ Rename
  ↓ Assign Texture
  ↓ Modify Transform
  ↓ Open Lua
  ↓ Modify Gameplay
  ↓ Save
  ↓ Play
  ↓ Observe Console
  ↓ Stop
  ↓ Save Project
  ↓ Close
  ↓ Cold Start
  ↓ Open Project
  ↓ 全部状态恢复
```

至少验证：Entity identity / Transform / Sprite GUID / Script contents /
Dirty state / Console output / Save-Reload / 冷启动恢复。

## 5. 晋升规则（AV-GP）

Phase 3 严格延续"UI 缺能力 ≠ Engine 缺能力"：

### 可以做
- 已有 ABI 能力的 Avalonia UI 化（Hierarchy/Inspector/Asset/Script/Console）。

### 不可以直接做
- 为 UI 看起来完整而新增 Engine API。

### 阻塞流程
记录 `AV-GP-xxx`，然后问：**冻结 API 能不能组合表达？**

```text
能 → UI 自己解决
不能 → 真实阻塞 → 可复现 → 最小能力 → Ledger → Engine 实现
       → Contract Test → Production Gate
```

## 6. 验收门槛（Phase 3 完成判定）

- P3-A..E 各面板闭合回路 GREEN；
- AV-G3 Production Authoring Gate ALL GREEN；
- test_bridge 全量回归 GREEN（ASan 开启）；
- AvaloniaEditor `--gate1` / `--gate2` 回归 GREEN；
- integrity gate 122/122 GREEN；
- C++/Engine 零改动（bridge 层内自足）；ImGui 零；JSON 零手改。

## 7. 本阶段明确不做

- Viewport / GPU interop（Phase 7）
- Play/Stop 运行时轨道（Phase 5；m_Playing edit-state 拒绝已就位）
- 多场景/多工程（Phase 6）
- Undo/Redo（DF08：非 P0）
- 反射式通用 Inspector（C++ 旧实现 537 行自动反射仅作参考）
- Hierarchy 树结构（无 parent/reparent 需求证据）
- IDE 级脚本编辑（高亮/补全/调试器/断点/LSP）
