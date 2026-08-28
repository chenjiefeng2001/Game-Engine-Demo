# Avalonia Phase 3 — AV-G3 Production Authoring Gate（Plan + 基线冻结）

> 建立日期：2026-08-28 · 分支：avalonia
> 前置：P3-A ✅ P3-B ✅ P3-C ✅ P3-D ✅（gate1/2/3a/3b/3c/3d ALL GREEN）
> 本文件 = **AV-G3 基线冻结（§1）与实施计划（§2–§7）**。§1 仅证据，无实现。

---

## 1. 基线冻结（Evidence, no feature）

AV-G3 的第一步只做证据、不改功能。基线如下（2026-08-28）：

### 1.1 Working tree 状态（source 改动，均为已验收的 P3-A~D 批次）

```text
 M bridge/include/editor_bridge/capi.h         （P3-D）
 M bridge/src/EditorSession.cpp                 （P3-D）
 M bridge/src/EditorSession.h                   （P3-D）
 M bridge/src/capi.cpp                          （P3-D）
 M editor_avalonia/MainWindow.axaml             （P3-D）
 M editor_avalonia/MainWindow.axaml.cs          （P3-D）
 M editor_avalonia/Native/EditorBridgeApi.cs    （P3-D）
 M editor_avalonia/Services/EditorHostService.cs（P3-D）
 M editor_avalonia/Services/Phase3Gate.cs       （P3-C/D gate）
 M editor_avalonia/ViewModels/MainViewModel.cs  （P3-D）
 M engine/src/Core/Application.cpp              （会话前既有改动，非 AV-G3 引入）
 M tests/test_bridge/test_editor_capi.cpp       （P3-D 3 新用例）
 M docs/Evidence-Baseline.md                    （基线修正：test_bridge 17→20）
 ?? docs/Avalonia-Phase3D-Charter.md
 ?? docs/Avalonia-Phase3-G3-Plan.md             （本文件）
```

scratch / build / log（spike_gp*/ *_scratch / selftest.log / logs/）被 .gitignore 覆盖，不计入。

### 1.2 完整 Integrity Gate（`tools/integrity_gate.ps1 -SkipBuild`）

```text
I1 PASS: 27 files all registered
I2 PASS: in-scope total = 162
    ok test_bridge 20  / test_content 42 / test_core 20 / test_e2e 6
    ok test_gp01 22 / test_job 4 / test_physics 16 / test_renderer 9 / test_scripting 23
    excl test_ecs (动态对抗 EntityManager API 改写无法冻结)
I3 PASS: DF01-DF09 + GP01 all backed by executed tests（全部通过）
EVIDENCE INTEGRITY GATE: ALL GREEN (I1+I2+I3), in-scope total = 162
```

> 基线修正：P3-D 新增 3 个 Runtime 契约用例后 test_bridge 实际 = 20，
> `Evidence-Baseline.md` 原记 17，**已修正为 20**（唯一引起 I2 red 的项，非功能改动）。

### 1.3 回归（`AvaloniaEditor --gateX`）

```text
gate1 exit=0  [PHASE1 GATE]  ALL GREEN
gate2 exit=0  [PHASE2 GATE]  ALL GREEN
gate3a exit=0 [P3-A GATE]    ALL GREEN
gate3b exit=0 [P3-B GATE]    ALL GREEN
gate3c exit=0 [P3-C GATE]    ALL GREEN
gate3d exit=0 [P3-D GATE]    ALL GREEN
```

### 1.4 契约测试

```text
test_bridge    20/20 PASS   （EditorBridgeTest.*，含 P3-D Runtime 三用例）
test_scripting 23/23 PASS   （S1-S5 + Gameplay G1-G8 + M001/M005）
```

> AV-G3 落地后（§8）test_bridge 升至 **21/21**（+`MissingGuidAssetWarnsAndEntityRemains`）；
> baseline 已同步更新（见 `Evidence-Baseline.md`）。

---

## 2. AV-G3 Golden Scenario（单一生产门）

不再夹带新 UI 功能；只验证现有模块「能否组合支撑一次完整生产会话」。

### Authoring（编辑态）

```text
Open Project → Open Scene
→ Create Entity → Rename Entity → Modify Transform
→ Import Asset → Assign Sprite → Assign Script
```

### Scripting（运行时）

```text
Open game.lua → 修改真实 gameplay 参数 → Save
→ Play → 验证行为变化
→ 修改 Lua → Reload → 验证 _PERSIST 保留
```

### Persistence（落盘闭环）

```text
Stop → Save Project → Close
→ Reopen → 验证：Entity / Name / Transform / Sprite GUID / Script GUID /
                Lua 内容 / Runtime-GameState
```

---

## 3. 负路径（必须，不只 Happy Path）

- **G3-E1 未保存修改**：Modify → DIRTY → Close → Cancel（内容仍在）→ Close → Discard（内容恢复/放弃）
- **G3-E2 Lua 错误**：Edit invalid Lua → Save → Play/Reload fails → Error 可见 → Editor 存活 → Fix → Reload → Success
- **G3-E3 Missing Asset**：既有 GUID 缺失契约 → warning → entity 保留 → Editor 可用

> **不为测试新增 Engine 能力。**

---

## 4. 零内部操作（AV-G3 完成标准，比之前严格）

整个 Golden Scenario：❌ 不编辑 JSON · ❌ 不调用 C++ API · ❌ 不使用 ImGui ·
❌ 不手动修改 GUID · ❌ 不手动修改 manifest · ❌ 不依赖外部 IDE ·
❌ 不重启 Editor “修状态”。

若为完成流程而绕过 UI / 直调 ABI，该 Gate 不算通过。

---

## 5. 摩擦记录（独立于测试失败）

每次命中不流畅处，记录并归档到 `docs/AV-G3-Ledger.md`（AV-G3 通过后亦保留）：

```text
AV-GP-xxx
Trigger:
Observed:
Evidence:
Severity:       [low|medium|high|blocker]
Frozen API sufficient?:  [yes|no]
Minimum UI fix:
Capability gap?:        [无 | 描述]
```

尤其盯：Entity selection 稳定 / Browser→Inspector→Script 选中连续 / Dirty 可理解 /
Import→Assign 自然 / Reload 后用户感知 / Save-Close-Reopen 心智模型一致 / 错误后如何恢复。

---

## 6. 最终裁决（只允许三种结果）

- **Result A — Production Ready**：流程顺畅，仅低级 UX 摩擦 → **Phase 3 FROZEN**。
- **Result B — UX Fix Required**：冻结 API 足够但 Workflow 明显阻塞 → 记录 AV-GP → 修 Avalonia（不动 Engine）→ 重跑 Gate。
- **Result C — Capability Gap**：UI 无法继续（冻结 API 根本不能表达）→ **不现场实现** → AV-GP → Ledger → 最小 Capability → Engine 实现 → Contract Test → AV-G3 Regression。

---

## 7. AV-G3 通过之后

不进 Phase 4；进入 **AV-Dogfood-01 — Real Production**：用 **Arena Survival v2**
从 Avalonia Editor 完成一次真实内容迭代（新增/改属性/导入/换 Sprite/改波次/改 Lua/
Play 验证/Save/Close/Reopen/再 Play），并记录七元组
（Trigger → Friction → Evidence → Severity → Existing API? → Minimal Fix → Repro），
产物汇入 AV-GP Ledger 后，再决定 Phase 4。

---

## 8. 实现位置（AV-G3 实施时，本文件不含代码）

- Gate 载体：`editor_avalonia/Services/Phase3Gate.cs` 新增 `RunProduction`（`--gate3g`）。
- 断言全部走 VM → Session ABI 单向路径（复用现有 `MainViewModel` / `EditorHostService`）。
- 负路径 E1/E2/E3 各自独立断言块；E3 复用既有 GUID 缺失契约（不新增 Engine）。
- 最终裁决结论写入本文件 §6 与 `docs/AV-G3-Ledger.md`。

---

## 9. 实施结果（已落地）

`AvaloniaEditor --gate3g` **ALL GREEN exit=0**：

- **Authoring**：Open→Create G3Hero→Rename G3Protagonist→Transform(1.5,0,-3.25)
  →Import texture+script→Assign Sprite/Script→Save。
- **Scripting**：Open game.lua→改写 director（`_PERSIST.hp`）→Save→Play（hp=14）→
  改 hp→Save→Reload（**`_PERSIST` 保留，14 不被 50 覆盖**）→Stop→Re-play（hp=50
  生效）。
- **Persistence**：Save→Close→Reopen→Name/Transform/Sprite/脚本绑定/GUID 稳定/
  game.lua 磁盘内容 == 保存内容；reopen→Play 用 hp=50。
- **负路径**：E1（dirty→Recover 已保存态）、E2（broken→error→存活→fix→恢复）、
  E3（缺失 GUID→**告警入 Console**＋实体保留＋Editor 可用）。

回归：gate1/2/3a/3b/3c/3d **全 exit=0**；test_bridge **21/21**；test_scripting 23/23；
Integrity Gate **ALL GREEN (163 tests)**；Evidence-Baseline test_bridge 21。

**Final Verdict → Result A — Production Ready → Phase 3 FROZEN。**
摩擦记录见 `docs/AV-G3-Ledger.md`（AV-GP-101~106，均低严重度，无 Capability Gap）。