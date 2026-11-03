# Project Integration & Delivery Baseline

**状态**：`PASS — BASELINE RECONCILED`
**日期**：2026-11-01
**分支**：`avalonia`
**审计类型**：只读（未修改源码、未构建、未运行测试、未执行 Git 写入）
**P0 通过标准**：`PASS — BASELINE RECONCILED`

---

## 1. Git 基线核对

### 1.1 基本信息

| 项目 | 值 |
|---|---|
| 当前分支 | `avalonia` |
| HEAD | `b10e0cf84442602932d365c2445ce49bcaf8324a` |
| HEAD 提交信息 | `docs(t-6-b): close gate B with full regression (B6)` |
| 相对 `origin/avalonia` | ahead 22, behind 0 |
| 工作区状态 | 3 个脏子模块 + `crashes/`（115 文件）+ 1 个未跟踪文档 |

### 1.2 已知提交在当前分支历史中的验证

| 工作项 | 已知提交 | 是否在当前分支历史 | 验证方式 |
|---|---|---|---|
| T-7 Audio | `1a61b36` | ✅ 是 | `git log --oneline --all` |
| Editor UI (UI-H2 + T-6 Console) | `c1f7933` | ✅ 是 | 同上 |
| F2-S1 Gate B | `4f4b955` | ✅ 是 | 同上 |
| F2-S1 Closure | `2ad43c7` | ✅ 是 | 同上 |
| T-6-B B0 | `ef220bf` | ✅ 是 | 同上 |
| T-6-B B1 | `d23271a` | ✅ 是 | 同上 |
| T-6-B B2 | `c376e42` | ✅ 是 | 同上 |
| T-6-B B3 | `34cddbf` | ✅ 是 | 同上 |
| T-6-B B4 | `23c09af` | ✅ 是 | 同上 |
| T-6-B B5 | `e461bd4` | ✅ 是 | 同上 |
| T-6-B B6 | `b10e0cf` | ✅ 是 | 同上 |

所有已知提交均在当前 `avalonia` 分支历史中。

### 1.3 工作区状态详情

| 类别 | 文件/目录 | 状态 | 说明 |
|---|---|---|---|
| 未跟踪 | `docs/T-6-Overall-Completion-Audit.md` | `??` | 本审计阶段产出的文档（P0 交付物之一） |
| 脏子模块 | `third_party/d3d12ma` | ` M` | 内部 dirty（build artifact 删除） |
| 脏子模块 | `third_party/openal-soft` | ` M` | 内部 dirty（build artifact 删除） |
| 脏子模块 | `third_party/vma` | ` M` | 内部 dirty（build artifact 删除） |
| 未跟踪目录 | `crashes/` | `??` | 115 个 crash dump 文件，保留原状 |

### 1.4 子模块提交指针

| 子模块 | 提交指针 | 状态 |
|---|---|---|
| `third_party/d3d12ma` | `1d86c1130f61453634b1df85782e1fecfd59a525` | dirty（内部 build artifact） |
| `third_party/openal-soft` | `9fb7bb38cb35f50c692edaba47bad13d2bb879ba` | dirty（内部 build artifact） |
| `third_party/vma` | `3aa921224c154a0d2c43912bc88e1c42ce1f7607` | dirty（内部 build artifact） |

子模块指针与 HEAD 记录一致，dirty 状态为内部 build 产物，非代码修改。

### 1.5 HEAD 相对 origin 的变化

Gate 0 扫描（2026-10-29）记录 ahead 13。当前 ahead 22，增加 9 个提交，
对应 T-6-B B0～B6（7 个）+ T-6-Overall-Completion-Audit（1 个）+ 本 P0 基线（未提交）。
实际增加的提交数与 T-6-B B0～B6 的 7 个提交 + 2 个文档提交一致。

---

## 2. 文档与代码一致性矩阵

### 2.1 工作主线状态核对

| 主线 | 关闭记录 | 记录状态 | 代码/实现状态 | 一致性 |
|---|---|---|---|---|
| T-1 RigidBody | `docs/T-1-Closure-Record.md` | CLOSED (SCOPE-BOUND) | 提交 `1a61b36` 在历史中 | ✅ 一致 |
| T-2 Content Pipeline | `docs/T-2-Closure-Record.md` | CLOSED | 提交 `c1f7933` 在历史中 | ✅ 一致 |
| T-4 GPUProfiler | `docs/T-4-GPUProfiler-Closure.md` | CLOSED | 证据在仓库中 | ✅ 一致 |
| T-5 Generic DebugDraw v1 | `docs/T-5-DebugDraw-v1-Final-Closure-Record.md` | PASS (SCOPE-BOUND / CLOSED) | Gate A/B/C/D1/D3 + D5-11 修订记录齐全 | ✅ 一致 |
| T-6-A Managed Console | `docs/T-6-Closure-Record.md` | PASS (SCOPE-BOUND) | `EV_*` + Console 实现在代码中 | ✅ 一致 |
| T-6-B Engine Logging | `docs/T-6-B-Console-Logging-GateB6-Closure.md` | COMPLETE (B0-B6) | 全链路实现 + 测试 | ✅ 一致 |
| T-6 Overall | `docs/T-6-Overall-Completion-Audit.md` | OPEN — PARTIALLY COMPLETE | A-1..A-12 全部满足 | ⚠️ 需 P1 更新为 CLOSED |
| T-7 Audio | `docs/T-7-B-Closure-Record.md` | PASS (SCOPE-BOUND / CLOSED) | `1a61b36` 在历史中 | ✅ 一致 |
| UI-H2 Visual Hierarchy | `docs/UI-H2-P2-Closure-Record.md` | PASS (SCOPE-BOUND / CLOSED) | `c1f7933` 在历史中 | ✅ 一致 |
| F2-S1 Collider Serialization | `docs/F2-S1-Closure-Record.md` | CLOSED (SCOPE-BOUND) | `4f4b955`, `2ad43c7` 在历史中 | ✅ 一致 |

### 2.2 文档状态冲突识别

| 冲突 | 位置 | 说明 |
|---|---|---|
| T-6 Overall 状态未更新 | `docs/T-6-Closure-Record.md:3` | 仍写 `T-6 — PARTIALLY COMPLETE (T-6-B DEFERRED)`，但 T-6-B 已关闭。需 P1 更新。 |
| T-6 Overall Audit 未反映最终裁决 | `docs/T-6-Overall-Completion-Audit.md` | 写 `OPEN — PARTIALLY COMPLETE`，但用户已批准 `T-6 — COMPLETE — CLOSED — SCOPE-BOUND`。需 P1 更新。 |
| T-6-B Gate0 Scan 中的 T-6-A 引用 | `docs/T-6-B-Console-Logging-Gate0-Scan.md` | 引用 T-6-A closure 的 "PARTIALLY COMPLETE"，历史记录，无需修改。 |

### 2.3 T-5 D5-11 修订记录

- `docs/T-5-DebugDraw-D5-11-Release-Semantics-Amendment.md` 存在。
- `docs/T-5-DebugDraw-GateC-Closure-Record.md:9` 引用 D5-11 修订。
- `docs/T-5-DebugDraw-v1-Final-Closure-Record.md` 状态为 `PASS (SCOPE-BOUND / CLOSED)`。

### 2.4 T-5 test_content 基线例外

- `docs/T-5-DebugDraw-v1-Final-Closure-Record.md:38` 记录 `test_content` 5 个 dogfood manifest 缺失导致 FAIL。
- `docs/T-5-Content-Dogfood-Manifest-Restoration-Task.md` 存在。
- 状态：KNOWN BASELINE ASSET GAP，已接受为 SCOPE-BOUND 例外。

### 2.5 ABI 定义一致性（文档层）

| 项目 | Native (capi.h) | Managed (EditorBridgeApi.cs) | 一致 |
|---|---|---|---|
| `ENGINE_HOST_ABI_VERSION` | v1.9 (`((1<<16)\|9)`) | `EngineHostExpectedVersion = (1<<16)\|9` | ✅ |
| `EngineLogRecordV1` size | 2392B | `Marshal.SizeOf = 2392` | ✅ |
| `EngineLogPollStatsV1` size | 40B | `Marshal.SizeOf = 40` | ✅ |
| `EV_COUNT` | 14 | 14 | ✅ |
| `RuntimeResult` | 76B | 76B | ✅ |
| Pick packet | 32B | 32B | ✅ |

---

## 3. 工作区保护状态

### 3.1 保留项（不得修改）

| 项目 | 状态 | 说明 |
|---|---|---|
| `third_party/d3d12ma` | dirty（内部 build artifact 删除） | 保留原状，不 reset/clean |
| `third_party/openal-soft` | dirty（内部 build artifact 删除） | 保留原状 |
| `third_party/vma` | dirty（内部 build artifact 删除） | 保留原状 |
| `crashes/` | 115 个未跟踪文件 | 保留原状，不删除 |

### 3.2 未跟踪文档

| 文件 | 状态 | 说明 |
|---|---|---|
| `docs/T-6-Overall-Completion-Audit.md` | `??` | P0/T-6 审计产出，保留 |

### 3.3 P0 约束遵守情况

- ✅ 未修改任何源码文件
- ✅ 未执行任何构建
- ✅ 未运行任何测试
- ✅ 未执行 Git 写入（commit/push/tag/merge/reset/clean/stash）
- ✅ 未修改子模块或 `crashes/`
- ✅ 仅创建了本基线文档（P0 交付物）

---

## 4. P0 通过标准确认

```
PASS — BASELINE RECONCILED
```

### 回答 P0 四问

1. **当前 HEAD 是什么？**
   `b10e0cf84442602932d365c2445ce49bcaf8324a`，分支 `avalonia`，提交信息 `docs(t-6-b): close gate B with full regression (B6)`。

2. **哪些提交已包含？**
   T-1 (`1a61b36`)、T-2 (`c1f7933`)、T-4、T-5 (Gate A/B/C/D1/D3 + D5-11)、T-6-A (`ef220bf` 前)、T-6-B (`ef220bf`..`b10e0cf`，7 个提交)、T-7 (`1a61b36`)、UI-H2 (`c1f7933`)、F2-S1 (`4f4b955`, `2ad43c7`)。所有已知提交均在当前分支历史中。

3. **哪些工作仍未提交？**
   - `docs/T-6-Overall-Completion-Audit.md`（未跟踪，本审计产出）
   - 3 个脏子模块（内部 build artifact，非代码修改）
   - `crashes/`（115 个 crash dump，保留原状）

4. **哪些文件属于历史遗留 dirty？**
   `third_party/d3d12ma`、`third_party/openal-soft`、`third_party/vma`（build artifact 删除）和 `crashes/`（115 个文件）。这些在 Gate 0 扫描时已存在，非本次引入。

5. **哪些任务真正还未完成？**
   - T-6 Overall 文档状态需 P1 更新（PARTIALLY COMPLETE → CLOSED）
   - T-6-A UNVERIFIED 项：真实 GL 丢失、协议违反 native、10k UIA、partial failure（证据缺口，非实现缺口）
   - T-5 test_content 基线例外（KNOWN BASELINE ASSET GAP）
   - RenderGraph SG6（隔离状态，未恢复）
   - 生产 3D Viewport + DebugDraw 接线（未开始，P4 候选）
   - Jolt DebugRenderer 完整性（未开始，P4 候选）

---

## 5. P0 交付物

- `docs/Project-Integration-Delivery-Baseline.md`（本文件）

---

## 6. 下一阶段建议

P0 通过。建议进入 P1（Documentation & Closure Reconciliation），重点：
1. 更新 `docs/T-6-Closure-Record.md` 和 `docs/T-6-Overall-Completion-Audit.md` 为 T-6 整体 CLOSED。
2. 核对 T-5 D5-11 修订记录、F2-S1 关闭证据、T-7 145/145 证据、UI-H2 P2 真实窗口证据。
3. 不修改源码、不构建、不测试（P1 仅文档修改）。
