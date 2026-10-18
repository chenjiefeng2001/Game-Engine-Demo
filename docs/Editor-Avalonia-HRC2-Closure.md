# HRC-2 CLOSED — Runtime Host Core Topology + Stage A Evidence Chain

> **阶段**：UI-HRC-2-A → HRC-2 收口（**纯文档 + 证据复核 slice，零代码改动**）
> **日期**：2026-09-28
> **状态**：`CLOSED`，**范围限定为 Windows hidden-GLFW** —— HRC-2-A Definition of done 的
> 第 **1 / 2 / 7** 项仍为 **OPEN（A8 门控：无真实 Linux/Xvfb 环境）**（逐项见 §2）
> HRC-2-A Readiness = `PASS`；G2P-01~07 GREEN；**G2P-08 DEFERRED**
> **证据链**：`docs/Editor-Avalonia-HRC2-StageA-A1-Closure.md`（F-1）→
> `docs/Editor-Avalonia-HRC2-StageA-A2-Closure.md`（F-2，7-7）→
> `docs/Editor-Avalonia-HRC2-StageA-A3-Closure.md`（UI-state）→
> `docs/Editor-Avalonia-HRC2-NHI-1-Charter.md`（Closure B）
> **不吸收**：Viewport / Renderer（HRC-3）、A8 跨平台 receipt、A3 的任何代码增强
> **命名消歧**：本文件的 HRC-2 与计划中的 **A3 = Persistent Hidden GLFW Context**
> （`Editor-Avalonia-HRC2-A3-Audit.md`，2026-09-24，native A3 已 COMPLETE）无关；
> Stage A/A3 指 UI-state 收口。

---

## 1. HRC-2 最终判定

| 能力 | 判定 | 主要证据 |
|---|---|---|
| Engine 实例与所有权（单 Host、UI 只发 command/收 event） | **CLOSED** | A0–A3 native；A7-2 managed host switch；`--prodpath` 单实例断言 |
| GL 生命周期 / Host-owned persistent context | **CLOSED** | A3（persistent carrier/owner/rollback/C ABI 边界）；`EngineHost_*` 导出；`GraphicsFailureRollback` |
| Runtime Tick 唯一 owner | **CLOSED** | `Hrc2A7.SinglePumpOwnerAndLifecycleRegression`；A2/F-2（唯一命名触发点 `EnsureRuntime()`、pump 随项目启停、仅 playing 时 tick） |
| MainWindow 真实接入（生产 UI 路径能驱动 Host） | **CLOSED** | **F-1 由 A1 关闭**（PP-01~PP-12 真实 XAML 事件路径）；**F-3 UI-state honesty 由 A3 关闭**（host-derived `IsOpen` / selection intent / create receipt） |
| Failure / Teardown 语义 | **CLOSED** | A6（transaction、OnDestroy exactly-once、callback barrier）；A7-2（三态 gate、generation guard、typed 失败）；A2（`Shutdown` 未完成不 `Destroy`、pump typed failure 检出） |
| typed failure 可观测性 | **CLOSED** | `HostOperationException(status/stage/diagnostic)`；A2 七场景；`test_editor_capi.cpp` 的 `EDITOR_BRIDGE_FAIL_FBO` → `GLFailed/FBO` 映射断言 |
| 跨平台（Linux/Xvfb）receipt | **DEFERRED** | A8 —— 无真实 Linux/Xvfb 环境；按计划"不阻塞"（见 §3 G2P-08） |

**HRC-2-A Readiness = PASS**（2026-09-28 复核：F-1 由 A1 关闭、F-2 由 A2 关闭、F-3 由 A3 关闭；
两个 blocking finding 均已 CLOSED）。

### 1.1 HRC-2-A Definition of done 逐项对照

计划的 DoD（`Editor-Avalonia-HRC2-Implementation-Plan.md` §11）共 10 项。**本轮满足 7 项，
3 项为 A8 门控、仍 OPEN** —— 因此 HRC-2 的 CLOSED 是**范围限定**结论，不是"全 DoD 达成"：

| # | DoD 项 | 判定 | 依据 |
|---|---|---|---|
| 1 | A0～A8 全部完成并有 evidence | **OPEN（A8）** | A0～A7 已完成；**A8 DEFERRED**（无真实 Linux/Xvfb） |
| 2 | HRC-G2P-01~08 全部 GREEN | **OPEN（A8）** | G2P-01~07 GREEN（Windows 范围）；**G2P-08 属 A8** |
| 3 | production Host 不再直接持有 `EditorSessionHandle` | **满足** | A7-2 managed host switch；`Program.cs` 单 `EditorHostService` 入口 |
| 4 | runtime thread / GL context / FBO / Session ownership 唯一 | **满足** | G2P-01 / G2P-02 证据（`CreationThreadContract`、`owner_thread_id`、Host-owned context） |
| 5 | typed `RuntimeResult` 与 failure injection 可查询 | **满足** | `sizeof(RuntimeResult)==76`；A2 七场景 + `EDITOR_BRIDGE_FAIL_*` 注入 |
| 6 | H1 Dispose→H2 Bootstrap 同进程通过 | **满足** | `RuntimeSeamProofThreeCycles`、`GlobalStateReturnsToBaseline` |
| 7 | Windows/Linux 实际 headless Gate 通过且无 skip | **OPEN（A8）** | Windows 通过；**Linux/Xvfb 环境不存在**，不以 skip 充数 |
| 8 | HRC-1 全部回归继续通过 | **满足** | `--hrc1` / `--selftest` / Gate 1~3g 全绿；native `test_bridge` 41/41 |
| 9 | 未覆盖或重置既有 Phase 7/HRC-1 dirty changes | **满足** | 未 `stash`/`reset`；native 源码改动 0；dirty baseline 保留 |
| 10 | 未创建未经请求的 commit/tag | **满足** | 无 commit / 无 tag |

**关闭 A8 门控项的确切条件**：存在真实 Linux/Xvfb（或等价 headless GL）环境 → 跑实际 Gate、
无 skip → 补 G2P-08 evidence。在此之前，Windows 范围结论有效，跨平台 receipt **不存在**
（不得用 Windows 结果替代）。

---

## 2. 证据链汇总（A1 → A2 → A3 → NHI-1）

| slice | 关闭的 finding | 证据入口 | 结果 |
|---|---|---|---|
| **A0–A3 native** | persistent carrier / owner / rollback / C ABI 边界 | `test_hrc`、`test_bridge` | 15/15、41/41 |
| **A4 / A6** | runtime seam proof / lifecycle hardening | `Hrc2A4.*`（3）、`Hrc2A6.*`（2） | PASS |
| **A7-0/1/2** | 32-call routing / native dispatch seam / managed host switch | `Hrc2A7.*`（5）、`EngineHostAbiTest.*` | PASS |
| **Stage A / A1（F-1）** | 生产 UI 路径无证据；2 个既有缺陷（Filtered\* 重建、`EV_ENTITY_CREATED` 漏 dirty） | `--prodpath` PP-01~PP-12 | ALL GREEN |
| **Stage A / A2（F-2）** | bootstrap 由用户动作触发；3 个真实缺陷（异常逃出 handler / pump typed failure 被丢弃 / `Shutdown` 未完成即 `Destroy`） | `--boot` 7 场景 | 7-7 GREEN |
| **Stage A / A3** | `IsOpen` 非单一事实源、create/auto-select 竞态、handler 缺 create 结果行 | `--uistate`（S0/S1/N1/N2/N4/N5） | 4-4 GREEN |
| **NHI-1** | A2 的 native 退出期"卡住"归因 | Closure B：ASan 37/12,225 vs Release 0/400 | CLOSED |
| **A3 runner flake** | 与 NHI-1 **分开**记录 | A3 Closure §6 | infrastructure observation |

---

## 3. HRC-G2P-01~08 逐项判定

判定只承认**已执行的证据**（native 测试用例 / managed gate 断言），不承认叙述性结论。

| Gate | 判定 | 已执行证据（用例 / 断言） |
|---|---|---|
| **G2P-01** offscreen GL seam | **GREEN** | Host-owned hidden persistent context（`InitializePersistentHiddenContext(64,64)`）+ **fixed-size 64×64** offscreen FBO；`EngineHost_GetRuntimeProofState` 暴露 `fboReady/fboId/fboWidth/fboHeight/readbackVerified`（测试断言 `fboId != 0`、`64×64`、`readbackVerified == 0` 即"尚未 pump 帧"）；FBO stage 失败注入（`BootstrapFailureRollback` / `GraphicsFailureRollback` / `EDITOR_BRIDGE_FAIL_FBO` → `GLFailed/FBO`） |
| **G2P-02** GL thread affinity | **GREEN** | `CreationThreadContract`（wrong-thread 创建 → `nullptr` + `WRONG_THREAD`/`STAGE_PLATFORM`）；`LegacyCreationRequiresRegisteredMainThread`；`owner_thread_id` 同一性断言；`EmergencyWithoutCurrentContextIsBounded`（un-current 路径有界） |
| **G2P-03** single frame-pump owner | **GREEN** | `Hrc2A7.SinglePumpOwnerAndLifecycleRegression`；`--boot`（唯一触发点、tick 仅 playing、CloseProject 停泵）；A1 PP-03 / PP-11 |
| **G2P-04** tick / ABI contract | **GREEN** | `ResultLayout`（`sizeof(RuntimeResult) == 76`）；`OpDispatchRoutesEverySessionCall`；A2 typed `status/stage/diagnostic`；dt clamp `0.1s` + raw/effective 暴露 |
| **G2P-05** init rollback | **GREEN** | `BootstrapFailureRollback` / `GraphicsFailureRollback` / `RuntimeResourceFailureRollback` / `Hrc2A4.ResourceFailureRollback`；A2 六注入场景（no half-init、glfw init/terminate 平衡、重试确定性） |
| **G2P-06** terminal teardown ordering | **GREEN** | `SkeletonLifecycle`、`GraphicsTeardownFailureIsObservable`；`Hrc2A6.SessionProofAndExactlyOnceStop`、`CloseDetachesBeforeStopCallback`；A2 `Shutdown` 门禁（未完成不 `Destroy`） |
| **G2P-07** global/static reset boundary | **GREEN** | `Hrc2A4.GlobalStateReturnsToBaseline`；`RuntimeSeamProofThreeCycles`（三周期 H1→H2，glfw users/init/terminate 回到 baseline）；A2 重开不重新 bootstrap |
| **G2P-08** CI / headless cross-platform | **DEFERRED** | 属 **A8**：无真实 Linux/Xvfb 环境。计划已判定"跨平台 receipt DEFERRED（不阻塞）" |

> **G2P-01~07 的 GREEN 范围是 Windows hidden-GLFW 单机**。Linux/Xvfb 侧的同项证据不在本判定内，
> 归 G2P-08 / A8；不得用 Windows 结果代替跨平台 receipt。

> **2026-09-28 更正（HRC-3 Step 1 审计时发现）**：G2P-01 的证据先前被表述为
> "readback verified"，**过强**。实测事实：生产路径的 readback 是
> **每帧 clear FBO 后对 (32,32) 单像素做 clear-color 校验**（`EngineHost.cpp:1141-1166`），
> 不是帧内容回读；FBO 是固定 **64×64 探针尺寸**，不是视口尺寸。G2P-01 依其自身措辞
> （"fixed-size offscreen FBO create/render/readback identity"）仍成立，但**不得**据此认为
> HRC-2 已证明任何"渲染真实呈现"——那正是 HRC-3 的对象，且当前**尚未成立**。

---

## 4. 收口当日一手复核（2026-09-28）

| 证据 | 结果 |
|---|---|
| native `test_hrc` | **15/15**（Debug 11.1s / Release 3.5s） |
| native `test_bridge` | **41/41**（Debug 11.7s / Release 5.1s） |
| native `test_scripting` | **27/27**（Debug 0.2s / Release 0.04s） |
| integrity gate I1 + I2 + I3 | **ALL GREEN**，in-scope 合计 **240 不变** |
| `--uistate`（A3） | **4/4 GREEN** |
| `--prodpath` / `--boot` / `--hrc1` / `--hrc2` / `--selftest` / Gate 3a / 3g | **全部 ALL GREEN / exit 0** |
| Release native target（`EngineCore` / `EditorBridge`） | exit 0 |
| native 源码改动 | **0** |
| residual process | **0** |
| `git diff --check` | exit 0 |
| fixture（`assets/gp01/*`） | 未被本轮写入 |

**复现注记（两条，避免下次误判为回归）**：

1. Debug native 套件需 `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1`（NHI-1 定的 runner 要求，
   `tools/integrity_gate.ps1` 已内置）。缺失时会在 `interception_win.cpp:193` 直接 CHECK fail，
   **不是**测试失败。
2. Release 全量构建在 `tests/test_ecs/ComponentLifecycleTest.cpp` 编译失败 —— 该文件
   **自 2026-07-16（commit `4d0db67`）未改动**，与 HRC-2 任何 slice 无关；HRC-2 相关的
   Release target（`EngineCore` / `EditorBridge`）构建 exit 0。

---

## 5. 冻结边界（后续 slice 不得静默改写）

1. **N3 evidence boundary**：`CreateEntityOutcome.Failed` 的映射存在于代码中，但
   **没有生产/native 注入证据**（两个 backend 均无 create 注入点）。A3 的失败语义以
   `Refused` 负例取证。若将来 native 新增 create 注入点，**必须**回头补该分支证据。
2. **process-exit hygiene observation**：偶发"gate 已 `exit=0` 但进程不退出"，无新 native stack，
   **不重开 NHI-1**、不是产品 workaround、不计入功能 verdict（runner 侧已分栏输出）。
   稳定复现或新 live native stack 才独立开调查 slice。
3. **ABI 冻结**：`ENGINE_HOST_ABI_VERSION = (1 << 16) | 1`；24 个 `EngineHost_*` 导出；
   27 个具体 op（capi.h 枚举 29 个 token = 27 op + `NONE` + `COUNT`）；
   `RuntimeResult` 76B。任何变更需要独立的 ABI slice + 两侧（native / managed mirror）同步证据。

---

## 6. 能力归属：HRC-2 已闭合 vs 移交 HRC-3

**HRC-2 已闭合（不再改动）**：单 Host 所有权与 command/event 拓扑、GL 生命周期与 Host-owned
persistent context、唯一 frame pump 与 tick 契约、typed failure / rollback / teardown 语义、
global reset 边界、生产 UI 路径可驱动 Host（A1）、bootstrap 触发契约（A2）、UI-state 诚实性（A3）、
native teardown 归因（NHI-1）。

**移交 HRC-3（Viewport Reality）**：viewport 的实际渲染真实性 —— render target 呈现、FBO readback
与 UI 呈现的一致性、viewport 交互与相机同步。**这些在本轮全部未打开**：A3 未顺带触碰
`ViewportPanel` / renderer 任何代码，HRC-2 收口也不得引入。

**A8（DEFERRED）**：Linux/Xvfb 跨平台 receipt。恢复条件仍是"存在真实 Linux/Xvfb 环境"。

---

## 7. 判定汇总

```text
HRC-2-P:                 COMPLETE / CORE TOPOLOGY FROZEN
A0–A3 native:            COMPLETE（persistent carrier / owner / rollback / C ABI 边界）
A4/A6:                   COMPLETE（runtime seam proof / lifecycle hardening）
A7-0/A7-1/A7-2:          COMPLETE（32-call routing / native dispatch seam / managed host switch）
Stage A/A1 (F-1):        CLOSED（--prodpath ALL GREEN）
Stage A/A2 (F-2):        CLOSED（--boot 7-7 GREEN；native teardown finding 由 NHI-1 关闭）
Stage A/A3 (F-3):        CLOSED（--uistate 4-4 GREEN；N3 证据边界已记录）
NHI-1:                   CLOSED / Closure B
HRC-2-A readiness:       PASS
HRC-2:                   CLOSED，范围 = Windows hidden-GLFW（DoD 7/10 满足；1/2/7 为 A8 门控 OPEN）
HRC-G2P-01~07:           GREEN（Windows hidden-GLFW 范围，逐项挂已执行用例）
HRC-G2P-08:              DEFERRED（A8，无真实 Linux/Xvfb）
I1 / I2 / I3:            GREEN（I2 = 240 不变）
native 源码改动:          0
next:                    HRC-3 Viewport Reality；A8 继续 DEFERRED（DoD 1/2/7 的唯一阻塞源）
```

**下一步**：**HRC-3 Viewport Reality**。不在 HRC-2 内继续加证据，也不以"顺手验证"为名打开
Viewport/Renderer —— 那属于 HRC-3 的范围与判定标准。
HRC-2-A 的**完整** DoD 达成仍挂在 A8 上；本文件不构成对 A8 的任何替代。
