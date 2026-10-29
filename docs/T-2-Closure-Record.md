# T-2 关闭记录：Content v2 M1 全阶段（SCOPE-BOUND）

**裁决日期**：2026-10-29（报告级评审；裁决方未独立读取本地源码或重跑测试）。
**T-2 状态**：`CLOSED — SCOPE-BOUND`。
**约束**：本裁决不授权新的功能阶段，也不授权 commit / push / tag / merge；
既有子模块脏标记与 `crashes/` 保持原状。

本记录只汇总**实际运行结果**。既有失败、跳过、未验证项与构建警告均按原始记录保留，
不因关闭而改写归因、不合并统计。

---

## 1. 阶段状态

| 阶段 | 状态 | 交付记录 |
|---|---|---|
| Stage 0 契约冻结 | 已冻结 | `docs/Roadmap-Decision-Record-2026-10-28.md` |
| M1-A ProjectRoot/路径规范化 | `CLOSED — SCOPE-BOUND` | `docs/Content-v2-M1-A-ProjectRoot.md` |
| M1-B Manifest 严格事务加载 | `CLOSED — SCOPE-BOUND` | `docs/Content-v2-M1-B-Strict-Load.md` |
| M1-C Rename + ScriptSave | `CLOSED — SCOPE-BOUND` | `docs/Content-v2-M1-C-Rename-Script-Save.md` |
| M1-D Revision/current 提交点 | `CLOSED — SCOPE-BOUND` | `docs/Content-v2-M1-D-Revision-Commit.md` |
| M1-E 导入服务汇点 | `CLOSED — SCOPE-BOUND` | `docs/Content-v2-M1-E-Import-Service.md` |
| M1-F 全链故障注入 | `CLOSED — SCOPE-BOUND` | `docs/Content-v2-M1-F-Fault-Matrix.md` |
| **T-2** | **`CLOSED — SCOPE-BOUND`** | 本文件 |

## 2. 最终证据（实际运行）

| 套件 | Debug | Release | 日志 |
|---|---|---|---|
| `FaultMatrixTest`（含 RF-1/2/3） | 14/14 | 14/14 | `build/m1f_test_matrix_debug.log` |
| `test_bridge` 全量 | 100/100 | 100/100 | `build/m1f_test_bridge_{debug,release}.log` |
| `test_content` | 92 通过 / **5 既有失败** / 1 skip | 同左 | `build/m1f_test_content_{debug,release}.log` |
| `test_pick_transport` | 9/9 | 9/9 | `build/m1f_test_pick_{debug,release}.log` |
| 完整 CTest | **16/17** | **16/17** | `build/m1f_baseline_ctest_{debug,release}.log` |

- 5 个既有失败：`ContentDogfood.DF03_Smoke_LoadRunTimerExpire`、
  `ContentDogfood.DF03_EntityCount_MultiScript`、`Dogfood05/06/07`
  （缺失 fixture，见 `docs/Handoff-CI-Remaining-Failures.md` §2），保持原状态。
- 构建：第一方全量重编 Debug/Release 各 **5 条既有警告**
  （1×C4005、2×C4477、2×LNK4006；`build/m1f_build_{debug,release}_full_final.log`）。
- 托管：`dotnet build editor_avalonia/AvaloniaEditor.csproj -c Debug` 成功；
  最近一次增量构建记录 4×NU1900（既有 NuGet 源告警）。**不将本轮描述为零失败/零警告。**

## 3. RF-1 / RF-2 / RF-3（接受为范围受限证据）

| 风险 | 证据 | 裁决 |
|---|---|---|
| RF-1 `current` 替换成功后回读不匹配 | `RF1_ReadbackMismatchEntersRecoveryAndBlocksWrites`：进入 `RecoveryRequired`，Save/Rename/Import/Play/Reload/ScriptSave 全部被拒；重启旧 revision | `PASS — SCOPE-BOUND` |
| RF-2 关键提交边界进程终止 | `M1FCrashHarness` 子进程在 5 个边界 `_Exit(90)`；重启验证旧/新完整 revision、首次发布标记边界、候选不晋升、rename 清理中断 | `PASS — SCOPE-BOUND` |
| RF-3 ScriptSave 三阶段失败 | `RF3_ScriptSaveStageFailuresKeepOriginal`：temp write / verify / replace 分别失败，原脚本与 `current` 不变、无临时残留 | `PASS — SCOPE-BOUND` |

四态判定均来自持久化 `current`/revision 与真实文件状态；未使用内存返回值、最高
Revision 或“回滚函数已执行”推断。

## 4. RF-4：正式接受范围例外

**状态**：`UNVERIFIED — ACCEPTED SCOPE EXCEPTION`。

**已有证据支持的范围**：
- `MainWindow.OnImportAsset` 不再自行复制文件，改为向 EngineHost op 11 提交源路径；
- EngineHost 专项测试证明该入口进入统一导入服务；
- 统一服务的路径验证、GUID 分配与 RevisionStore 提交已有测试。

**未验证范围（不得表述为已通过）**：
- 真实窗口启动；
- 文件选择器操作；
- 托管事件处理；
- UI → 原生导入服务的完整运行时链路。

**禁止出现在任何关闭/验收材料中的表述**：
- “托管 UI 端到端导入通过”；
- “所有生产导入路径均已完成端到端验证”；
- “EngineHost 专项可以代替 UI 自动化验收”。

**后续任务**：真实 `editor_avalonia` 导入 UI 的自动化/可重复运行验证单独登记
（覆盖真实文件选择、导入请求、成功/失败反馈、工程重载后的资产可见性），按独立验收
标准关闭；不追溯修改本次 T-2 证据。Reattach 托管 UI 仍不存在，继续作为已声明的产品
能力限制，不扩大本次范围。

## 5. 物理断电级持久性边界

- **已验证**：报告中列出的进程终止边界及重启后的提交状态（RF-2）。
- **未验证**：真实断电、操作系统崩溃、存储设备缓存丢失等情形下的目录项与文件数据
  持久性。
- **不作承诺**：未经专项验证，不宣称已满足断电级持久性保证。RF-2 不等价于断电证明。

## 6. 保留的范围限制 / 残余风险

1. **RF-4**：托管 UI 端到端导入未验证（已接受范围例外，见 §4）。
2. **物理断电级持久性**：未验证、不作保证（见 §5）。
3. **UNC ProjectRoot 实际共享打开**：M1-A 已列为风险台账；仅验证形式规则与 PathKey，
   未在真实 UNC 共享上执行端到端打开/保存；依赖 UNC 文件系统行为的事务提交验收前需补测。
4. **Revision/失败候选 GC**：未实现；不得为清理空间误删 `current` 引用的版本。
5. **写锁范围**：仅约束遵守锁协议的写入者；绕过协议的外部写入不保证互斥。
6. **FNV-1a 64**：仅用于意外损坏检测，不提供密码学防篡改。
7. **Reattach 托管 UI**：不存在，仅 C ABI 显式入口；产品能力限制。
8. **导入能力边界**：默认目录 `assets/imported/tex|scripts`；同路径重复导入幂等但不更新/
   不覆盖；无内容哈希去重。
9. **工具/离线路径**：`ScriptSandboxApp`、`AssetBakery` 直接使用 `ContentRegistry`，
   不属编辑器生产导入路径，未做故障注入。
10. **运行期外部篡改 `current`**：启动时校验；运行期若被外部篡改，下一次保存按 M1-D
    基线复验规则处理（stale/RecoveryRequired），不承诺自动修复。

## 7. 关闭声明

T-2 以 `CLOSED — SCOPE-BOUND` 收口：M1-A…M1-F 的实现与证据已封存，RF-1/2/3 已补证，
RF-4 与物理断电级持久性按上述范围例外/边界保留。

本关闭**不表示**：零失败、零警告、托管 UI 端到端通过、断电级持久性保证、或所有产品
导入工作流已完全验收。后续 UI 端到端验证与产品能力扩展需另立任务，并按各自验收标准
独立关闭。
