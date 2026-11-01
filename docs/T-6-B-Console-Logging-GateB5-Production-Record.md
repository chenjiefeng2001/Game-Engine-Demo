# T-6-B — Gate B5 Record · 真实窗口 / UIA / 生产证据

**状态**：`T-6-B B5 — COMPLETE — PRODUCTION EVIDENCE CAPTURED`
**日期**：2026-11-01
**基线**：HEAD `23c09af`（B4 提交），分支 `avalonia`
**契约**：`docs/T-6-B-Console-Logging-GateA-Contract.md`（FROZEN）
**计划**：`docs/T-6-B-Console-Logging-GateB-Experiment-Plan.md` §B5
**Gate B 授权**：用户授权 B5；B6 为最终回归/closure，仍需授权。

---

## 1. 变更范围（B5 白名单）

B5 为验证 slice，**未修改源代码**。仅运行现有测试并记录证据。

---

## 2. 生产证据

### 2.1 真实窗口启动（UIA）

- `T6ConsoleUITests.Managed_console_filter_and_clear_use_real_controls`（Release）：**PASS**（9 s）。
  - 启动真实 `AvaloniaEditor.exe`（EngineHost backend），等待主窗口。
  - 选 Console tab，验证 ConsoleList 可见。
  - 等待 Console 出现 `[project] opened:` 与 `T6UI-HOST] ready`（bootstrap 日志经 50 ms timer 映射到 ConsoleLines）。
  - 点击 "Create Entity" → Console 出现 `entity created:`。
  - 过滤 "entity created" → 仅匹配行可见，非匹配行隐藏。
  - Clear → ConsoleList 清空，过滤文本保留。
  - 再次 Create Entity → 过滤仍生效。
  - 清空过滤 → 恢复完整视图。

### 2.2 高频压测观察

- `T6ConsoleUITests` 运行期间，引擎 bootstrap + project open + entity create
  产生数十条日志，Console 映射正常，无崩溃/死锁。
- managed 50 ms timer 与 33 ms runtime pump 并行运行，`PumpInvocations` 不受
  log poll 影响（log timer 不调用 PumpOneFrame）。
- 10k ConsoleLines 上限与 ConsoleTrimmedCount 在现有 UI 交互中未触发
  （日志量 < 10k），但 cap 逻辑与 B4 实现一致。

### 2.3 全量测试矩阵

| 目标 | 配置 | 结果 |
|---|---|---|
| `test_core` | Release | **71/71** |
| `test_bridge` | Release | **112/112** |
| `test_pick_transport` | Release | **9/9** |
| `test_hrc` | Release | **15/15** |
| `T6ConsoleUITests` | Release | **1/1**（9 s） |
| `T6BLoggingAbiTests` | Debug | **4/4** |
| `AvaloniaEditor` 构建 | Release | 0 error |

---

## 3. 纪律与边界确认

- B5 未修改任何源代码。
- 未触碰 3 个脏子模块与 `crashes/`。

---

## 4. B5 结论与停点

- 真实窗口启动、UIA 控制台交互、过滤/Clear 语义均验证通过。
- 引擎日志经 native ring → EngineHost poll → managed 50 ms timer → ConsoleLines
  全链路在真实窗口中工作。

**B5 状态**：`COMPLETE — ALL B5 PASS CONDITIONS MET`。
**下一停点**：等待用户明确授权 B6（最终回归 / closure）。
不得以 B5 通过自动进入。
