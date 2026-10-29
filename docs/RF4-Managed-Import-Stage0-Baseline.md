# RF-4 后续（任务 #141）：Stage 0 只读基线

**核验日期**：2026-10-29
**状态**：`GATE 0 — READ-ONLY BASELINE VERIFIED`
**范围**：为任务 #141（Managed UI Import E2E）建立当前工作区可信基线。
**约束**：本文件不授权实现；未修改源码/测试/构建；未运行构建或测试；
未执行 commit / push / tag / merge；未清理、重置或改写任何工作区内容。

本文件只记录**本轮独立读取**的工作区事实。T-2 的测试数字只引用关闭记录，不在本轮重跑。

---

## 1. Git 基线

| 项目 | 观测值 | 证据 |
|---|---|---|
| 分支 | `avalonia` | `git rev-parse --abbrev-ref HEAD` |
| HEAD | `e9acfb5049a2d0a8695aa1575c8de3a1d8dfe0a0` | `git rev-parse HEAD` |
| HEAD 提交 | `docs(t-1a): record RigidBody contract and deferral review` | `git log -1 --oneline` |
| 暂存区 | 空 | `git diff --cached --stat` 无输出 |
| 已跟踪修改 | **38 个条目**（35 个文件 + 3 个子模块） | `git status --porcelain` |
| 未跟踪条目 | **39 个**；展开为 **153 个文件**（含 `crashes/` 115 个） | `git status --porcelain`；`git ls-files --others --exclude-standard` |
| `crashes/` | **115 个既有产物**，未跟踪，未触碰 | `find crashes -type f \| wc -l` |
| 脏子模块 | `third_party/d3d12ma`、`third_party/openal-soft`、`third_party/vma` | `git status --porcelain` 的 3 个 ` M third_party/...` |
| 其它 worktree | `D:/repos/p7_ui`（分支 `avalonia-p7`，HEAD `c47fbb9`）独立存在 | `git worktree list` |
| stash | `stash@{0}`（`On master`，ECS 签出前的旧暂存） | `git stash list`；与 #141 无关，未触碰 |

**未解释但已记录的既有改动**：`.gitignore` 处于已修改状态；本基线不假定其来源，也不改动它。
若后续发现它与 #141 无关，应在 #141 交付记录中保持原样，不纳入本任务变更。

> 基线纪律：不得用 `git reset --hard`、`git clean` 或任何等价操作“清理”上述状态。
> 本轮没有改动任何已跟踪文件或未跟踪文件。

---

## 2. T-1 与 T-2 的封存状态

### 2.1 T-1

| 项目 | 观测值 | 证据 |
|---|---|---|
| 最终状态 | `T-1 — CLOSED — SCOPE-BOUND` | `docs/T-1-Closure-Record.md:3` |
| Managed UI A | `PASS`（Debug/Release `--t1ui`） | 同上 §10、`docs/T-1-Managed-UI-Acceptance.md` |
| Managed UI B | `UNVERIFIED — ACCEPTED SCOPE EXCEPTION` | 同上 §11 |
| 规则 | B 例外不等于 managed UI 物理 E2E 通过；不新增 Bridge ABI/EngineHost 诊断能力 | 同上 §11 |

### 2.2 T-2

| 项目 | 观测值 | 证据 |
|---|---|---|
| 最终状态 | `T-2 — CLOSED — SCOPE-BOUND` | `docs/T-2-Closure-Record.md:3` |
| RF-4 | `UNVERIFIED — ACCEPTED SCOPE EXCEPTION` | 同上 §4 |
| RF-4 的未验证范围 | 真实窗口启动、文件选择器、托管事件处理、UI→原生导入完整运行时链路 | 同上 §4 |
| 后续任务 | 真实 `editor_avalonia` 导入 UI 的自动化/可重复验证单独登记 | 同上 §4 |
| 物理断电持久性 | 未验证、不作保证 | 同上 §5 |

---

## 3. T-2 关键文件哈希（与 T-1 Stage 0 记录逐项比对）

基准值来自 `docs/T-1-Stage0-Baseline.md` §1.3；本轮重新计算当前文件：

| 文件 | 当前 SHA-256 | 与 T-1 Stage 0 记录 |
|---|---|---|
| `engine/src/Core/Content/RevisionStore.cpp` | `d03bbe873959599fea0cc719a73e9210996f8cf4ad5bf222c7a437289368b1f2` | **MATCH** |
| `engine/include/Engine/Core/Content/RevisionStore.h` | `aa5c2d838c1f998ad2887d4540d205944c5fa84c4368e021eb2e26cdbd4f1ec0` | **MATCH** |
| `engine/src/Core/Content/AtomicFile.cpp` | `b7d109df1f05ee82673c4eff4d403aad26c7657d56949dbb42217adb86461eb9` | **MATCH** |
| `engine/src/Core/Content/ProjectPaths.cpp` | `0abfd67987ec4d2c0e29a3a715a3c2ffdb05df3e80289869827ae9050c857036` | **MATCH** |
| `engine/src/Core/Content/Utf8Path.cpp` | `dd67d4040b419d95369262cfd198303a58785ff9630f73a4730e7098c3521121` | **MATCH** |
| `bridge/include/editor_bridge/capi.h` | `02dbce3118a4fe7d1038f23efcff3c7e5b03dfdb44315c2226515c79604b2886` | **MATCH** |
| `docs/T-2-Closure-Record.md` | `355c513f0a22c2ca95d41b7ccfd6b5bd7ffbba85c453c3b489661d56a862222c` | **MATCH** |
| `tests/test_bridge/test_revision_commit.cpp` | `8c8fd870fe633c1d2021577f48c840770272d224ff64dd269eb8099cfae71b3a` | **MATCH** |

结论：T-2 的 Content 代码、ABI 头、关闭记录与 revision 测试文件未因 T-1 或文档工作发生字节变化。

> 说明：这里比对的是 **T-1 Stage 0 记录中的基准值**，不是重新执行 T-2 Stage 0。
> 本轮未重跑 T-2 测试，也不改写 T-2 的任何数字。

---

## 4. 现有 T-1/T-2 交付物与 #141 的隔离

当前工作树包含：

- T-1 源码/测试/文档（PhysicsRuntime2D、RigidBody、T-1 gates 等）；
- T-2 源码/测试/文档（Content v2 M1、RevisionStore、导入服务等）；
- `crashes/` 115 个既有产物；
- 三个既有脏子模块；
- 一个与 #141 无关的 `stash@{0}`；
- 一个独立的 `p7_ui` worktree。

**#141 的新增文件必须可区分**。当前尚未创建任何 #141 文件；Gate 0 不产生源码或测试。
后续若批准实现，建议只新增：

- `tests/EditorAvalonia.IntegrationTests/`（或经裁决的等价测试项目）
- `editor_avalonia` 中仅用于启动 scratch 工程的 harness 入口（若选择该方案）
- `tools/run_m1import_gate.ps1`
- `docs/RF4-Managed-Import-*.md`

不得修改 T-2 的 Content/RevisionStore/导入服务语义，不得修改 T-1 的 RigidBody/物理契约。

---

## 5. Gate 0 通过条件

| 条件 | 结果 |
|---|---|
| 当前基线可记录、可比较 | **PASS**（本文件） |
| T-1 关闭记录与实际文档状态一致 | **PASS**（`CLOSED — SCOPE-BOUND`；B 为已接受例外） |
| T-2 原有成果没有意外变化 | **PASS**（八个关键文件哈希全部 MATCH） |
| 已区分新任务文件与既有工作区内容 | **PASS**（§4；当前无 #141 文件） |
| 未修改源码、未运行迁移、未清理工作区 | **PASS**（本轮只读） |

**Gate 0 结论**：`PASS`。可以进入 #141 的范围冻结；未获 Gate A 批准前不进入实现。

---

## 6. 本阶段未做

- 未构建、未运行任何测试或 gate；
- 未修改源码、测试、构建脚本、WER/ASAN 配置；
- 未触碰 `crashes/`、脏子模块、`stash@{0}`、`p7_ui` worktree；
- 未执行 commit / push / tag / merge / reset / clean / stash。
