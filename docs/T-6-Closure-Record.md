# T-6-A Console 关闭记录

**状态**：`T-6-A — PASS — SCOPE-BOUND`；`T-6 — PARTIALLY COMPLETE (T-6-B DEFERRED)`
**裁决依据**：`docs/T-6-Console-GateA-Contract.md`（FROZEN，D-1…D-5 APPROVED WITH D-5 REQUIREMENTS）
**日期**：2026-10-29
**范围**：现有 `EV_*` 事件 + ViewModel 本地行的 managed Console；不含 Engine `spdlog` 接入。
**未执行**：commit / push / tag / merge；未修改 Bridge ABI、Engine Core、T-2/T-1/T-4/F2-S1。

本记录只写实际修改与实际运行结果。T-6-B（Engine 日志接入）未启动，不能据此宣称
managed Console 是完整 Engine 日志流。

---

## 1. 范围与冻结决定

| 项 | 结果 |
|---|---|
| D-1 范围 | 只处理 `EV_*` + ViewModel 本地行；Engine `spdlog` 未接入 |
| D-2 容量 | 10,000 行，超限淘汰最旧；事件/本地/边界行都计数 |
| D-3 工程切换 | 分段保留；native 切换提交时追加边界；GL 失败使用部分失败标记 |
| D-4 过滤 | `StringComparison.OrdinalIgnoreCase` 子串；只影响显示 |
| D-5 generation | managed 两阶段提交；以 `EV_PROJECT_LOADED` 为 native 提交证据 |

功能分界不变：Console 不是完整 Engine 日志 Console；T-6-B 仍未授权。

---

## 2. 实现

### 2.1 D-5：打开意图代次与两阶段提交

新增 `OpenAttemptTracker`（`editor_avalonia/Services/EditorHostService.cs:197`）：

- `Begin()` 创建唯一 `attemptId` 与 `candidateGeneration = activeGeneration + 1`；
  已有待决尝试时抛 `InvalidOperationException`，不允许两个代次竞争。
- `Admit()` 在 native 回调到达时判定归属：
  - 未收到 `EV_PROJECT_LOADED` 前的事件使用旧代次；
  - `EV_PROJECT_LOADED` 标记本次尝试 native committed，并与其后事件一起进入待决队列；
  - 待决事件不立即 `Dispatcher.Post`，避免在定案前显示。
- `Resolve(callSucceeded)` 按冻结状态表定案：
  - marker 已收到 + 调用成功 → commit；
  - 无 marker + 调用失败 → abort，旧代次不变；
  - marker 已收到 + GL 失败 → **仍 commit**；
  - 调用成功但无 marker → abort + `ProtocolViolated`，门禁失败。
- commit 时先切 active generation，再按原顺序放行待决事件；abort 时丢弃并计入 dropped。
- teardown 中止待决尝试并 bump 代次。

`EngineHostChannel.OpenProjectAttempt`（`EditorHostService.cs:1193`）与
`ApplyResolution`（`:1282`）把上述状态机接到 C ABI：

- commit 时解除 `CloseProject` 留下的 `_suppressEvents`，否则 reopen 的
  `EV_PROJECT_LOADED` 会被 teardown 抑制吞掉（门禁首轮实际失败后修正）。
- `OpenProjectOutcome` 记录 `AttemptId / CandidateGeneration / ActiveGeneration /
  NativeCommitted / CallSucceeded / ProtocolViolated / Status / Stage / Diagnostic`；
  外层 `EditorHostService.OpenProjectCore` 在 `finally` 中暴露给 ViewModel。
- D-3 边界行：
  - 成功：`[project] opened: <manifestPath>`；
  - native 已提交但调用失败：`[project] active: <manifestPath> — GL initialization failed`
    （非 GL 的 post-commit 失败使用 `open failed after native commit`）；
  - 加载前失败：不写成功边界。

### 2.2 Console 模型与 UI

| 项 | 实现 | 位置 |
|---|---|---|
| 原始缓存 | `ConsoleLines` 保留为原始集合，容量 10,000，超限丢最旧 | `MainViewModel.cs:38,1691` |
| 过滤视图 | `ConsoleView` 派生视图，UI 绑定它；原始缓存不变 | `MainViewModel.cs:39` |
| 过滤 | `ConsoleFilter` trim 后 `OrdinalIgnoreCase` 子串；匹配消息文本而非时间戳 | `MainViewModel.cs:41` |
| 清空 | `ClearConsole()` 清原始+视图，重置 trim 计数，保留过滤文本 | `MainViewModel.cs:1712` |
| 工程边界 | 成功打开/部分失败边界行，由 VM 写入 | `MainViewModel.cs:678-712` |

UI：Console Tab 增加真实 `ConsoleFilterBox` / `ConsoleClearBtn`，列表绑定
`ConsoleView`；AutomationId 为 `ConsoleFilterBox`、`ConsoleClearBtn`、`ConsoleList`
（`MainWindow.axaml:213-226`）。`ToolbarCreateEntityBtn` 增加 AutomationId 供 UIA 使用。

### 2.3 测试与运行器

| 文件 | 内容 |
|---|---|
| `editor_avalonia/Services/T6ConsoleGate.cs`（新增） | tracker 状态表 + 真实窗口门禁；`--t6console` |
| 同上 | `T6ConsoleHostBootstrap`：`--t6ui-host <scratch>`，只打开 scratch，不提供测试旁路 |
| `tests/EditorAvalonia.IntegrationTests/T6ConsoleUITests.cs`（新增） | 真实进程 UIA 过滤/清空 |
| `tools/run_t6_console_gate.ps1`（新增） | 构建 + 真实窗口 gate + UIA；`Start-Process -Wait` 保证 GUI 进程退出后再跑 UIA |

### 2.4 变更白名单

实际修改/新增仅：

- `editor_avalonia/ViewModels/MainViewModel.cs`
- `editor_avalonia/Services/EditorHostService.cs`
- `editor_avalonia/MainWindow.axaml`
- `editor_avalonia/MainWindow.axaml.cs`
- `editor_avalonia/Services/T6ConsoleGate.cs`（新增）
- `tests/EditorAvalonia.IntegrationTests/T6ConsoleUITests.cs`（新增）
- `tools/run_t6_console_gate.ps1`（新增）
- `docs/T-6-Console-Gate0-Scan.md`、`docs/T-6-Console-GateA-Contract.md`、
  `docs/T-6-Closure-Record.md`

无 Bridge/Engine 源文件改动；`capi.h` 哈希保持 T-2 基线。

---

## 3. 测试矩阵与实际结果

### 3.1 直接门禁

| ID | 证据 | 结果 |
|---|---|---|
| T6-A-01 容量 | 真实窗口 gate：10,005 行后 raw/view 均 10,000；`cap-5` 为最旧保留、`cap-10004` 为最新；trim 计数 5 | PASS |
| T6-A-02 清空 | 真实 `ConsoleClearBtn`：清空 raw/view、保留过滤、后续行仍显示；host dirty/counts 不变 | PASS |
| T6-A-03 过滤 | 真实 `ConsoleFilterBox`：大小写不敏感；raw 不变；取消过滤恢复；时间戳不参与匹配 | PASS |
| T6-A-04 过滤+清空 | 清空后过滤文本保留；匹配行出现、不匹配行不出现 | PASS |
| T6-A-05 打开成功 | 边界行 1 次、`EV ProjectLoaded` 1 次、dropped 不增加、generation +1 | PASS |
| T6-A-06 切换 | 旧代次事件 dropped 且不显示；新事件正常 | PASS |
| T6-A-07 加载前失败 | 缺失 scene：generation 不变；无新边界/ProjectLoaded；旧工程仍 open | PASS |
| T6-A-08 open/close 重复 | 重新打开：边界与 ProjectLoaded 各只增加 1；无重复投递 | PASS |
| T6-A-09 异步顺序 | 两个 `EV_*` 显示顺序保持 | PASS |
| T6-A-10 teardown | close 后事件不显示；dropped 不因抑制增加 | PASS |
| T6-A-11 UIA | 真实进程过滤/清空/清空后事件可见 | PASS（组合限制见 §4.4） |
| T6-A-12 绑定 | `ConsoleList.ItemsSource` 是 `ConsoleView`；raw 保留 | PASS |
| T6-A-13 加载后 GL 失败 | tracker 状态表：marker + 调用失败 → commit；真实 GL 失败未注入 | **PASS（状态表）/ UNVERIFIED（真实 GL）** |
| T6-A-14 协议违约 | tracker：成功无 marker → `ProtocolViolated`，不 commit | PASS（tracker） |
| T6-A-15 边界事件时序 | tracker：pre-commit 旧代次、marker 后候选；真实打开验证 ProjectLoaded | PASS |
| T6-A-16 防重复打开 | tracker：第二个 `Begin` 被拒 | PASS（tracker） |
| T6-A-17 候选事件暂存 | tracker：marker 后事件入队，commit 放行；abort/teardown 丢弃 | PASS |

### 3.2 运行结果

| 配置 | 命令 | 结果 | 日志 |
|---|---|---|---|
| Debug | `--t6console` 真实窗口 | exit=0；tracker + 全部场景通过 | `build/t6console_gate_Debug.log` |
| Release | `--t6console` 真实窗口 | exit=0；同上 | `build/t6console_gate_Release.log` |
| Debug | UIA `T6ConsoleUITests` | 1/1 PASS | `build/t6ui_test_Debug.log` |
| Release | UIA `T6ConsoleUITests` | 1/1 PASS | `build/t6ui_test_Release.log` |
| Debug | `--prodpath` | exit=0，ALL GREEN | `build/t6_reg_prodpath_debug.log` |
| Release | `--prodpath` | exit=0，ALL GREEN | `build/t6_reg_prodpath_release.log` |
| Debug | `--t1ui` | exit=0，T-1A PASS | `build/t6_reg_t1ui_debug.log` |
| Release | `--t1ui` | exit=0，T-1A PASS | `build/t6_reg_t1ui_release.log` |
| Debug | `--hrc2` | exit=0，ALL GREEN | `build/t6_reg_hrc2_debug.log` |
| Release | `--hrc2` | exit=0，ALL GREEN | `build/t6_reg_hrc2_release.log` |
| Debug | #141 UIA runner（含 M1 + T6 UI） | 2/2 PASS，exit=0 | `build/m1import_gate_Debug.log`；`build/t6_final_m1import_debug.log` |
| Release | #141 UIA runner（含 M1 + T6 UI） | 2/2 PASS，exit=0 | `build/m1import_gate_Release.log`；`build/t6_final_m1import_release.log` |
| Debug | `test_bridge` | 103/103 PASS | `build/t6_final_test_bridge_debug.log` |
| Release | `test_bridge` | 103/103 PASS | `build/t6_final_test_bridge_release.log` |

### 3.3 构建

| 配置 | 结果 | 警告 |
|---|---|---|
| Debug `dotnet build editor_avalonia --no-incremental` | 成功，0 error | **6 warnings**：2×NU1900、1×CS8602、3×CS0414（全部既有） |
| Release `dotnet build editor_avalonia --no-incremental` | 成功，0 error | **6 warnings**：同上 |
| UIA 测试项目 Debug/Release | 成功，0 error | 2×NU1900（既有） |

不宣称零警告；不宣称 clean build。T-6 新增代码没有新增编译诊断。

---

## 4. 限制与 UNVERIFIED

1. **T-6-B 未实现**：Engine `spdlog` 日志不进入 managed Console。Console 只显示
   `EV_*` 与 ViewModel 本地行；不能宣称统一 Engine 日志流。
2. **真实 GL 失败未注入**：T6-A-13 的分支由 tracker 状态表验证；没有在真实 Windows
   宿主制造“native 已打开、GL 失败”的现场，因此部分失败边界行的真实 UI 呈现为
   `UNVERIFIED`。
3. **协议违约未在 native 现场制造**：T6-A-14 为 tracker 单测；没有修改 native 去
   故意省略 `EV_PROJECT_LOADED`。
4. **T6-A-11 的组合场景**：真实控件的过滤/清空与清空后事件已验证；但“清空后立刻用
   文件选择器完成 #141 导入”未在同一用例中组合验证。独立的 #141 导入 UIA
   Debug/Release 均 PASS。
5. **容量测试层级**：10,000 行容量在 VM + 真实 ListBox 绑定层验证，未通过 UIA 创建
   10,000 个可见项（UIA 性能与虚拟化不适合该规模）。
6. **未实现**：级别/来源/正则过滤、序号、源端时间、折叠/去重、自动滚动、导出/持久化。
7. **Partial failure 的宿主状态**：native 已提交但 GL 失败时，T-6-A 提交代次并写部分
   失败边界，但不修复既有 managed host 的 `IsOpen/State` 语义；该路径真实现场未验证。
8. **UI 测试依赖交互桌面**：UIA 测试在无交互桌面时为 Inconclusive；本轮为 PASS。

---

## 5. 测试过程中保留的失败

| 现象 | 原因 | 处理 | 证据 |
|---|---|---|---|
| reopen 后 `EV ProjectLoaded` 只显示 1 次 | `CloseProject` 留下 `_suppressEvents=1`，commit 时未解除 | commit 时解除 teardown 抑制 | `build/t6console_gate_debug_01.log` |
| `ConsoleList` 控制不存在 | ListBox 只有 AutomationId，没有 `x:Name` | 增加 `x:Name` | `build/t6console_gate_debug_02.log` |
| UIA 读不到列表文本 | 依赖 `ControlType` PropertyCondition 在虚拟化 ListBox 上返回空；部分项在视口外 | 改 TrueCondition + 滚动到底 | `build/t6ui_test_debug_01..06.log` |
| Release runner 首次失败 | PowerShell `&` 不等待 GUI gate，gate 与 UIA 实例重叠 | 运行器改 `Start-Process -Wait` 并检查日志 | `build/t6_final_runner_release_console.log`；修正后 `..._02.log` PASS |

以上失败均已修正；未用重跑覆盖首次失败记录。

---

## 6. 基线保护

| 项 | 观测 |
|---|---|
| HEAD | `74ad0999b35b1783ea9e58667966d3e84917dea8`（`avalonia`） |
| T-2 八文件 SHA-256 | **全部 MATCH**（含 `capi.h` = `02dbce31…b2886`） |
| `crashes/` | 115 个产物，未触碰 |
| 脏子模块 | `third_party/d3d12ma`、`openal-soft`、`vma`，未触碰 |
| Git 写入 | 无 commit/push/tag/merge/reset/clean |
| repo fixture | 未写；gate/UIA 只用系统临时目录 scratch 并清理 |

---

## 7. 结论

- T-6-A 按冻结契约完成：D-5 两阶段代次、容量、清空、过滤、工程边界行、真实窗口与
  UIA 证据均通过。
- T-6 整体仍为 `PARTIALLY COMPLETE`：T-6-B（Engine 日志接入）未启动，未新增 ABI。
- 未验证项按 §4 保留；不将 tracker 单测表述为真实 GL/协议违约现场。
- 未执行任何 Git 写操作。

---

## 8. 证据索引

| 事实 | 文件:行 / 日志 |
|---|---|
| tracker 状态机 | `editor_avalonia/Services/EditorHostService.cs:197-356` |
| 两阶段打开与 outcome | `EditorHostService.cs:1193-1290` |
| teardown 代次 | `EditorHostService.cs:1078-1084` |
| Console 容量/视图/过滤/清空 | `editor_avalonia/ViewModels/MainViewModel.cs:38-52,1691-1730` |
| 边界行 | `MainViewModel.cs:678-712` |
| UI 控件 | `editor_avalonia/MainWindow.axaml:213-226` |
| 真实窗口门禁 | `editor_avalonia/Services/T6ConsoleGate.cs:16-322` |
| UIA 测试 | `tests/EditorAvalonia.IntegrationTests/T6ConsoleUITests.cs` |
| 冻结契约 | `docs/T-6-Console-GateA-Contract.md` |
| Gate 0 扫描 | `docs/T-6-Console-Gate0-Scan.md` |

---

## 9. T-6 Overall 最终关闭章节 (P1, 2026-11-01)

### 9.1 T-6 整体最终状态

**T-6 — COMPLETE — CLOSED — SCOPE-BOUND**

T-6-A（Managed Console）与 T-6-B（Engine Logging）均已关闭。T-6 的合同验收标准（A-1..A-12，来自 T-6-A 与 T-6-B 两份合同）全部满足。

### 9.2 T-6-A 历史状态保留

T-6-A 原始状态 PARTIALLY COMPLETE (T-6-B DEFERRED) 保留在本记录的历史部分（§1-§8），不覆盖。T-6-B 已于 2026-11-01 关闭（docs/T-6-B-Console-Logging-GateB6-Closure.md）。

### 9.3 接受的 KNOWN UNVERIFIED

| 项 | 来源 | 状态 |
|---|---|---|
| 真实 GL 丢失 → Console | T-6-A T6-A-13 | KNOWN UNVERIFIED |
| 协议违反 native 验证 | T-6-A T6-A-14 | KNOWN UNVERIFIED |
| 10k 行 Console UIA 验证 | T-6-A T6-A-01 | KNOWN UNVERIFIED |
| Partial failure host 状态 | T-6-A T6-A-13b | KNOWN UNVERIFIED |
| UI automation Inconclusive treated as PASS | T-6-A §4.8 | KNOWN UNVERIFIED |

### 9.4 合同外功能（不阻塞关闭）

Console 搜索、日志导出、Filter 持久化、Channel 过滤、EV/Engine 自动去重、Auto-scroll 暂停控制。这些不在合同验收范围内，属于 T-6 扩展方向。

### 9.5 关闭依据

- T-6-A：本记录（PASS — SCOPE-BOUND）
- T-6-B：docs/T-6-B-Console-Logging-GateB6-Closure.md（COMPLETE — ALL GATES PASSED）
- 全量回归：native 207/207 + managed 4/4 + UIA 1/1（Debug/Release）
- ABI v1.9 冻结，EV_COUNT=14、RuntimeResult=76B 不变
- 审计：docs/T-6-Overall-Completion-Audit.md（已更新为 CLOSED）

### 9.6 引用

- docs/T-6-Overall-Completion-Audit.md — T-6 Overall 完成审计（已更新为 COMPLETE — CLOSED — SCOPE-BOUND）
- docs/Project-Integration-Delivery-Baseline.md — P0 基线审计
