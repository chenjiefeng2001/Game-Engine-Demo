# Evidence Baseline — 权威测试计数基准

> 建立日期：2026-08-24 · 维护纪律：**本文件中的数字必须与实际 gtest discovery 一致**
> 由 `tools/integrity_gate.ps1` 强制校验（I2 门禁）
>
> 原则：**"测试文件存在" ≠ "测试存在"。只有被构建系统注册、被运行器发现并执行，才算 Evidence。**

> 原则：**native build 成功 ≠ production managed process 已加载最新 native/managed pair。**
> 任何 D3 gate 在采信结果前，必须先证明被测 artifact 是当前构建（见下方
> 「D3 Gate Contract · Artifact Freshness」）。**只看 build 命令的历史 exit code 不被接受。**

---

## In-Scope 目标（门禁强制校验）

| Target | 期望计数 | 状态 | 承载证据 |
|--------|---------|------|---------|
| test_scripting | 27 | VERIFIED | Scripting API v2.1 C-ABI/MVP/Gameplay + F1 通用 `Engine.component.*` + Camera 实证 + F2 Collider 实证（通用 component API，无特化域） |
| test_physics | 54 | VERIFIED | GPU Physics v1.x CPU/GPU ring + F2 Collider Contract/Binding/ColdRestart/Behavior（F2-B B2-B6，EF-F2B-003/004/006/007/009，见 Component-Ledger） |
| test_renderer | 9 | VERIFIED | GL46Device + RenderGraph SG6 隔离验证 |
| test_content | 42 | VERIFIED | Content Pipeline v1 + Resource Lifecycle + Editor Workflow + DF05/06/07 + DX Golden Gate + VS01 |
| test_job | 4 | VERIFIED | JobSystem 压力 |
| test_e2e | 6 | VERIFIED | 引擎启动生命周期 |
| test_gp01 | 22 | VERIFIED | Game Production GP-P1 · GP01 生产契约（GP1-A/B/C/D + DX 审计 remediation D7） |
| test_bridge | 44 | VERIFIED | EditorBridge C-ABI 契约 + A3/A4/A6 Host proof、failure rollback、emergency no-current、teardown/global/session probes；**HRC-3 P1 激活：新增 `ProductionSceneFrameRendersSessionContent`（生产 scene 渲染进 presentation FBO 的像素级验收），41 → 42**；**HRC-3 P2-Native：PresentationFramePacket 契约（op 28 / status 11 / 1,920,016B）2 条，42 → 44**（2026-09-29 修正：此前本行停留在 42，与实际 discovery 44 不符，导致表内合计与总数 243 相差 2） |
| test_hrc | 15 | VERIFIED | HRC-2-A4/A6/A7 Runtime Seam Proof：三次同进程循环、headless GL/FBO/session/frame、typed tick/readback、callback detach、transaction/reload failure、session/global proof、exactly-once stop/close；A7-1 op-dispatch seam 全 op 路由、pending-edit queue enqueue→pump→execute、GL-context affinity、frame 边界 `graphicsCurrent==0`、queued-edit failure 可观测、diagnostic 统一 |
| test_core | 20 | VERIFIED | GLM 数学库 + StackAllocator（2026-08-28：Vector3Test 默认构造误读未初始化内存 → 改为值初始化后纳入） |
| test_pick | 8 | VERIFIED | **HRC-3 D3 Step 3.1：native known-pixel pick proof**。刻意**不**链接 EditorBridge（native-only，不经 C ABI）：known pixel → 精确 `GameObject::ID` · 背景 → 精确 `-1` sentinel · Y 映射公式判别 · 越界/非有限/NotReady · 0 永不为有效 hit · pick pass 真实写入。**I2 由 243 → 251** |
| **合计** | **240** | | |

## Excluded 目标（记录原因，不计入基线）

| Target | 状态 | 原因 | 跟进条件 |
|--------|------|------|---------|
| test_ecs | EXCLUDED | 测试对旧 EntityManager API 编写，无法编译 | ECS 解除 deferred 后重写 |

## Dogfood → 可执行证据映射（I3 门禁强制校验）

每个 Dogfood 的冒烟结论必须能指向一个**已注册且可执行**的 gtest：

| DF | 主题 | 承载测试（target : filter） |
|----|------|---------------------------|
| DF01 | Entity.find + UI.text | test_scripting : `ScriptingGameplayTest.*` ＋ test_content : `GameplayAPIV2.M001_EntityFind_ByName` |
| DF02 | GUID→Resource→Restart 全链 | test_content : `ContentGolden.R13Golden_*` ＋ `DXGoldenGate.R13_Process*` |
| DF03 | 多实体/Timer/Win-Lose | test_content : `ContentDogfood.DF03_*` |
| DF04 | DX Audit（八项摩擦） | test_content : `DXGoldenGate.Full_Workflow_EmptyProject_To_PlayableGame`（DX-G1..G4 内联）|
| DF05 | Arena Survival | test_content : `Dogfood05.LoadAndRun` |
| DF06 | Multi-type Combat | test_content : `Dogfood06.LoadAndRun` |
| DF07 | Tower Defense + Restart round-trip | test_content : `Dogfood07.LoadRunRestart` |
| VS01 | Vertical Slice 01 产品现实门（menu→arena→boss 全流程） | test_content : `VS01.*` ＋ `VS01Debug.MinimalReloadRepro`（F1 回归守卫） |
| GP01 | Game Production Phase 1 生产契约（GP-P1 章程 §11，随阶段逐个点亮） | test_gp01 : `GP01.*` |

## 2026-08-28 整改记录（ASan/CTest 基础设施硬化）

起因：`test_bridge.exe` 直跑（无 `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1`）
在 MSVC ASan interception 层硬失败挂死（`interception_win.cpp:193`），并遗留
冻结进程锁死 `gp01_editor_scratch`。诊断与处置见
`docs/Avalonia-Phase3-Freeze-ASan-Report.md`。本次整改：

1. `tests/CMakeLists.txt`：ASan+MSVC 时对所有测试 target 设 CTest
   `ENVIRONMENT ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1` → ctest 直跑永不卡死；
   同时统一 `WORKING_DIRECTORY = 仓库根`（此前 ctest 在 build/tests 下运行，
   相对资产路径全部落空 → 假失败）。
2. `tools/run_test.cmd`：直跑 ASan 测试 exe 的统一启动器（main() 内设置 env
   已太晚，必须在进程启动前生效）。
3. test_core 从 EXCLUDED 恢复：exit=3 首测即死实为 `vec3 v;` 未初始化读
   （注释错误宣称 GLM 会零初始化）→ 改为 `vec3 v{}`，20/20 PASS。
4. test_bridge 首次纳入基线（16 用例，含 P3-B/P3-C 契约；P3-C 收官时
   新增会话内资产 index 稳定契约测试 → 17 用例）。
5. 合计 122 → 159。

**长期纪律**：Windows/MSVC ASan 测试必须经统一 launcher / CTest environment
启动；"裸跑 test exe 无 env" 不作为可靠证据。

---

## 2026-08-24 整改记录（Evidence Integrity Gate 建立时发现）

以下问题由本次审计暴露并当场修复：

1. `ResourceLifecycleTest.cpp`（M2 证据文件）从未注册 → 已注册；
   ContentPipelineTest.cpp 中 4 个内联 RLCycle 测试剥离至该文件（单一事实源）。
2. `DXGoldenGateTest.cpp`（M4 五道门禁）从未编译——引用未定义 helper（DX_P 等）、
   错误类型名（ResourceGuid）、HandleSpawn 绑定 null scene → 修复后注册，
   **M4 冻结声明首次获得可复现证据**（3 tests PASS）。
3. `DogfoodDXTest.cpp` 为 DXGoldenGate 的早期草稿，从未编译且无独有覆盖 → 删除并记录于此。
4. DF06 无任何可执行测试 → 新建 `Dogfood06Test.cpp`（15 实体 600 帧）。
5. `dogfood06_game.lua` 含非法 Lua 语法（第 12 行 `──` 非 `--` 注释；C++ 语法混入）→ 修复。
   该脚本在 DF06 当时的"通过"实际只来自场景加载路径，玩法脚本从未被真正执行过。
6. `EngineBootTest.cpp` / `JobSystemStressTest.cpp` 对已漂移 JobSystem API 编写
   （Dispatch→Schedule、Wait(nullptr)→WaitAll）→ 修复，两目标恢复构建并通过。
7. test_e2e 此前从未被纳入任何计数声明。

**教训**：DF01–DF04 时代的"全绿"存在幸存者偏差——从未参与编译的文件不产生链接错误，
也就从不暴露。Integrity Gate 即为此而设。

## 2026-09-25 A4 Runtime Seam Proof

- `test_hrc` 纳入 CTest，固定尺寸 hidden FBO、runtime-bound session、frame/readback probe、三次 H1→H2 同进程循环与 resource failure rollback。
- `test_bridge` 当前 discovery 为 41；`test_hrc` 为 9；基线合计更新为 234。
- Linux CI 配置 `ENABLE_TESTS=ON`、Xvfb 与 software GL；不以 skip 作为 HRC-2 evidence。
- A4 尚未完成全平台实跑前，HRC-G2P-01~08 与 HRC-2-A readiness 仍保持 NOT GREEN。

## 2026-09-26 A7-1 Native Op-Dispatch Seam

- `EditorBridge` 新增 4 个纯 additive 导出：`EngineHost_Dispatch` / `EngineHost_Fetch` / `EngineHost_PollPendingEdit` / `EngineHost_GetPendingEditProofState`；`RuntimeResult` 布局未变（76B，offsetof 16/48/68 仍由 `test_bridge` 断言）。
- 27 个 editor session op 全部经 EngineHost 的既有 `Submit()` executor 进入 native runtime thread，无第二 executor。
- GL 依赖 edit（`AssignSprite`）改走 pending-edit queue：仅在 `PumpOneFrame` 已 make-current 的窗口内执行；`accepted` 与 `executed/failed` 为两个独立结果，未排空 edit 在 CloseProject / 线程退出时被显式标记 failed，不静默丢弃。
- A4 的 "frame boundary `graphicsCurrent==0`" 不变量未变；A6 lifecycle / transaction / callback barrier 全部回归通过。
- `test_hrc` 9 → 15（A4 7 + A6 2 + A7 6）；`test_scripting` 27；基线合计更新为 240。
- Debug/Release 双配置：`test_hrc` 15/15、`test_bridge` 41/41、`test_scripting` 27/27；`dotnet build` 0/0；HRC-1 九个 gate 全 exit 0。

---

## HRC-3 P1 激活后的基线变更（2026-09-28）

**激活前基线（保留，不改写）**：`test_bridge` active **41**、DISABLED 1（`DISABLED_ProductionSceneFrameRendersSessionContent`）；`test_hrc` 15；`test_scripting` 27；I2 合计 **240**。

**激活后基线（当前）**：`test_bridge` active **42**、DISABLED **0**；`test_hrc` 15；`test_scripting` 27；I2 合计 **241**（41 → 42，净 +1）。

**变更原因**：HRC-3 P1 生产 scene 渲染验收用例由 DISABLED 转为 ACTIVE（门控
`kEnableProductionSceneRender` 同时由 `false` 改为 `true`）。该用例做**像素级**验收
（每变体独立清空后统计非 clear 像素 + 颜色直方图 + 二次读回确定性），
不接受 clear-only 成功。

**验收证据**：门控 ON 下 `test_bridge` 全量 **Debug 20/20 绿、Release 20/20 绿**（每次 42/42）；
`RuntimeSeamProofThreeCycles` 与 `ProductionSceneFrameRendersSessionContent` 均在全量套件内通过；
`test_hrc` 15/15、`test_scripting` 27/27；C ABI 仍为 v1.1（`((1 << 16) | 1)`），无增量。

**同批次的用例期望修正**（不改产品语义）：`DirtyStateContract` 补回与本文件其它 20 处
`MakeScratchFacing()` 调用点一致的 CWD 恢复（此前泄漏进程 CWD）；P1 验收用例的帧状态断言由
`== SUCCESS` 改为"非失败"（`SUCCESS` 或 `SKIPPED_NOT_PLAYING`），以符合 HRC-2 冻结的帧状态契约。

- A7-2（managed Host 切换）尚未开始：production managed caller 仍未迁到 `EngineHost*`，因此 28-call routing 在 managed 侧尚未闭合。

## HRC-3 P2-Native 之后的基线变更（2026-09-28）

**变更前基线（保留，不改写）**：`test_bridge` active **42**、DISABLED 0；I2 合计 **241**。
**变更后基线（当前）**：`test_bridge` active **44**、DISABLED 0；`test_hrc` 15；`test_scripting` 27；I2 合计 **243**（42 → 44，净 +2）。

**新增的两个 native contract 用例**：
`P2AbiVersionIncrementIsAdditiveOnly`（ABI 增量只增不改的静态断言）、
`P2FrameFetchIsAtomicAndSignalsNoNewFrame`（fetch 原子性 + no-new-frame 语义 + 像素真实性）。

**ABI increment（明确记录）**：EngineHost **v1.1 → v1.2**。
增量内容**只有一个**：在既有 `EngineHost_Fetch` 的 op 空间内新增
`ENGINE_HOST_OP_GET_PRESENTATION_FRAME = 28`（`OP_COUNT` 28 → 29）。
**没有**新增 export function；**没有**改 `RuntimeResult`（仍 76B，字段/顺序/packing 全不变）；
**没有**改任何既有 op 的编号/容量/语义；`ENGINE_HOST_STATUS_NO_NEW_FRAME = 11` 是**追加**取值。
`EditorBridge_GetVersion`（EditorBridge 自身）**未变**，仍 v1.1 —— 两套版本号独立。

**因 ABI increment 而必须同步的既有断言**：`SkeletonLifecycle`、`CreationThreadContract`
原本硬编码 `(1 << 16) | 1`，已改为引用单一来源 `ENGINE_HOST_ABI_VERSION`（`result.abi_version` 同理）。
managed 侧 `EditorBridgeApi.EngineHostExpectedVersion` 同步为 `(1 << 16) | 2`
（否则 `EditorHostService` 会在 bootstrap 抛 ABI 版本不匹配）。`ExpectedVersion`（EditorBridge 自身）保持 v1.1。

**验收证据**：门控 ON + ABI v1.2 下 `test_bridge` 全量 **Debug 10/10 绿、Release 10/10 绿**（每次 44/44）；
`test_hrc` 15/15、`test_scripting` 27/27；`dotnet build` exit 0（仅既有警告）。
**P2-Managed 镜像与 P2 UI proof 尚未开始**（分层保持：P2-Native contract → P2-Managed fetch → P2 UI proof）。

---

## P2 进入 standing 回归面（2026-09-28）

P2 已从"实验 gate"升级为 **production-path capability**，因此 `--p2frame` / `--p2ui`
被提升进**常驻回归面**，由独立 runner 驱动：

| 项 | 值 |
|---|---|
| runner | `tools/run_p2_gates.ps1`（**必须用 PowerShell 7 / `pwsh` 运行** —— `$psi.ArgumentList` 在 PS 5.1 不存在） |
| 场景 | `p2frame`（M1~M7 managed fetch 镜像）、`p2ui`（U1~U8 UI proof） |
| 进程隔离 | 每个场景**独立进程**（NHI-1 教训：GL/GLFW 拆卸污染） |
| 重试 | **刻意没有** retry/delay —— 失败必须保持失败并被阅读 |
| GREEN 判据 | exit 0 **且** stdout 含各自 marker（`[P2-MANAGED GATE] ALL GREEN` / `[P2-UI GATE] ALL GREEN`） |
| 实测 | **3/3 GREEN**（单次 9~24s；无 timeout） |
| native inventory | **0 增量** ⇒ `test_bridge` 仍 **44**、I2 仍 **243**、ABI 仍 **v1.2** |
| runtime 行为 | **未改动**（本 slice 只做 gate wiring + 文档） |

**已知的既有现象（非本次引入）**：首次运行偶发"已打印 green 但进程 linger"，
与 HRC-2 Closure §6 记录的 *runner / process-exit hygiene observation* 同类；
在放大 timeout 并多次复跑后未再出现，因此**未**为此加入任何产品或 runner 侧的重试/延迟。



## 2026-09-26 A7-2 Managed Host Switch

- `EditorHostService` 改为单 backend 策略：生产默认 `EngineHost`，HRC-1 gate flags 保留 legacy 注入链；backend 启动时选定一次，运行期无回退。
- 编辑面下沉到 `IEditorOps`；`EngineHostChannel` 内不存在任何 legacy `EditorSession_*` 调用，"无 legacy fallback" 成为结构性保证。
- 三态 `Starting / Ready / Stopping` 落地为 `HostRuntimeNotReadyException` / `HostRuntimeUnavailableException`；不可用状态不再表达为 `false`/`null`/空串。
- 唯一 pump 由 service 持有的 `DispatcherTimer` 驱动；legacy viewport timer 仅在 legacy backend 启动。
- `GetLastError` / `GetRuntimeError` 统一走 `EngineHost_GetLastDiagnostic`；generation guard 完全在 managed 侧实现（capture → `Dispatcher.UIThread.Post` → UI 侧比对）。
- 新增 `--hrc2` gate（`Services/Hrc2Gate.cs`）覆盖 G2-01~G2-10，`ALL GREEN`。
- 修复两个 A7-2 期间暴露的真实缺陷：teardown 事件导致 UI 线程未处理异常；以及首版修复中 `_suppressEvents` 未在重新打开 project 时清零。
- 回归：`test_hrc` 15/15、`test_bridge` 41/41、`test_scripting` 27/27（Debug + Release）；ctest Debug 10/10 in-scope；9 个 HRC-1 managed gate 全 exit 0；`dotnet build` 0 errors。
- 已知遗留：`MainViewModel._viewportCamX/Y/Z` 等 4 条告警属 dirty baseline 的 Phase 7 viewport WIP，Viewport 范围外，未处理。

## 2026-09-26 阶段 A / A1 生产 UI 路径证据（关闭 F-1）

- 新增 managed gate `--prodpath`（`Services/ProductionPathGate.cs`）：PP-01~PP-12 全部动作经
  **真实 XAML 控件事件**驱动（`Button/MenuItem.RaiseEvent(ClickEvent)`、Hierarchy/Asset ListBox
  选择变化、RenameBox 双向绑定、`window.Close()`），在 EngineHost backend 下验证
  `MainWindow` handler → `MainViewModel` → `EditorHostService` → native `EngineHost` 全链路。
- 新增只读诊断面（无 ABI 变更）：`EditorHostService.LastFrameProof` / `RuntimeCounts`
  （复用既有 `EngineHost_GetLastFrameResult` / `EngineHost_GetRuntimeProofState`），
  使「UI timer 驱动的唯一 pump 真的在推进帧」成为可执行证据。
- 证据暴露并当场修复 2 个既有缺陷（均与 backend 无关、此前无 gate 覆盖）：
  `ResetSession()` 未重建 `Filtered*` 视图（关闭工程后 Hierarchy/Asset Browser 残留旧条目）；
  `EV_ENTITY_CREATED` 缺 `RefreshDirtyState()`（工具栏 `+ Entity` 后 DIRTY 徽标不亮）。
- 记录未修 2 项（留 A3 / UI 诚实性后续）：create+auto-select 在异步事件下竞态；handler 路径
  缺 create 结果/失败行。
- native 侧零改动：`capi.h` / `EngineHost.*` / `test_hrc` / `test_bridge` 未触碰，
  `ENGINE_HOST_ABI_VERSION` 仍 `(1<<16)|1`，native 计数不变（`test_hrc` 15、`test_bridge` 41、
  I2 in-scope 合计 240）。
- 回归：`--prodpath` 两次连续 ALL GREEN；`--hrc1` / `--hrc2` / `--selftest` /
  `--gate1/2/3a/3b/3c/3d/3g` 全 exit 0；`dotnet build` 0 errors（4 条既有 Phase 7 WIP 告警）；
  `test_hrc` 15/15、`test_bridge` 41/41（Debug + Release）；`ctest` Debug 10/10 in-scope
  （`test_ecs` 仍为既有 Not Run）；integrity gate I1+I2+I3 ALL GREEN（240）；
  repo fixture mtime/hash 未变；targeted process = 0；`git diff --check` exit 0。
- 详见 `docs/Editor-Avalonia-HRC2-StageA-A1-Closure.md`。

## 2026-09-28 阶段 A / A3 UI-State Contract（CLOSED，D1/D2/D3 实现 + N3 方案 A）

- A3 contract §8 的 D1~D4 已 ratify，实现落地（managed-only，**native 零改动**）：
  - **D1**：`IsOpen` 由 `Entities.Count > 0` 改为 host 派生（`host.IsOpen && State == Ready`）；
    唯一 reconcile 例程 `ReconcileWithHostState` / `ClearProjectProjection`，open 成功/失败、
    runtime fault、事件忽略分支、close/reset 全部走它；四态不变量（Closed/Opening/Open/Fault）
    写入 gate 断言。
  - **D2**：auto-select 改为 **selection intent（entity index）**，刷新时解析；实现期发现必须区分
    两个解析点（create 返回后 `strict:false` 保留意图 / 刷新时 `strict:true` 丢弃不可满足意图），
    否则 EngineHost 的 return-before-event 顺序会提前丢弃意图。
  - **D3**：create 收敛为 `MainViewModel.CreateEntity(string?) -> CreateEntityReceipt`
    （`Created/Refused/Failed` + index/name/diagnostic，空 diagnostic 被工厂拒绝）；
    `MainWindow.OnCreateEntity` 不再直连 `_host.CreateEntity`，`CreateEntityFromVm` 收敛为兼容包装。
- 新增 managed gate `--uistate`（`Services/UiStateGate.cs`）+ runner
  `tools/run_a3_uistate_gate.ps1`（3 场景 + N2 跨运行**机械比较**，UTF-8 BOM）：
  enginehost（N1/N2a/N4 + Refused + 生产 teardown）、frame-fault（N5，`EDITOR_BRIDGE_FAIL_FRAME`）、
  legacy-order（N2b，`--uistatelegacy`）。N2 的证明是规范的 `A3-ORDER-FACT` 行比对：
  `order=return-before-event` vs `event-before-event`（两种顺序都真实执行过），
  而 `outcome/newIndex/finalSelection/intentResolved/intentPending` **逐字段相同**。
- `ProductionPathGate` PP-05 由 **KNOWN GAP 转为硬断言**（receipt + 意图解析 + 自动选中），
  A1 记录的 create/auto-select 竞态至此闭环。
- 与既有契约的冲突已按既有契约解决：A2/F2-08 要求终态 Dispose 后 VM 保留最后状态快照，
  故 `_host.IsDisposed` 时 reconcile 不动投影（修复前 `--boot Happy` 失败，修复后 7-7 恢复）；
  A1/A2 冻结的日志文案 `"+ Entity ignored: no project"` 保持不变。
- 回归：`--uistate` 4-4 GREEN；`--prodpath` ALL GREEN（含 PP-05 新断言）；`--boot` 7-7 GREEN；
  `--hrc2` / `--hrc1` / `--selftest` / `--gate1` / `--gate2` / `--gate3a..3g` 全 exit 0；
  integrity gate I1+I2+I3 ALL GREEN（**240 未变**）；Release native `EngineCore` / `EditorBridge` exit 0；
  `dotnet build` 0 errors（4 条既有告警）。
- **N3 已裁决（方案 A）**：host 侧 create 失败**无真实触发点** —— `EditorBridge` 唯一 legacy
  注入点是 `EditorSession_OpenProject`（`capi.cpp` 中 `LegacyFailureInjected()` 唯一调用处），
  `EditorSession_CreateEntity` / `EngineHostOpCreateEntity` 均无注入点；`CreateEntity` 只在
  "无 project"时失败，而 managed 层在更早的 D1 判断处已 `Refuse`。故 N3 收敛为 **Refused 负例**：
  S0（无 project）与 N5（faulted host）两个真实可达的拒绝态共用 `UiStateGate.AssertNoPhantomState`
  （diagnostic 非空 / 无新增实体 / 无假选择 / 无选择意图 / 无意外 bootstrap）。
  `CreateEntityOutcome.Failed` 的映射保留在代码里但**明确标注无生产证据**；原计划的
  `CreateFailureAsync` 断言体已删除（无触发点即死代码）。遗留风险：将来若 native 新增 create
  注入点，必须回头补 `Failed` 分支的真实证据。
- 环境注记（NHI-1 同类）：runner 偶发遇到"gate 已 ALL GREEN 且 `exit=0` 已打印但进程不退出"，
  单独复跑 4~5 s 正常；发生在 `Environment.Exit` 之后，非 A3 行为回归。runner 显式等待进程表清空、
  最多重试 1 次并在汇总表标注 `(1 retry)`，门禁判定以 `selftest.log` 为准。
- 详见 `docs/Editor-Avalonia-HRC2-StageA-A3-Closure.md`（**A3 CLOSED**），
  contract 与实施细节见 `docs/Editor-Avalonia-HRC2-StageA-A3-Contract.md` §9~§11。
- **process-exit 观察独立记录**（A3 Closure §6）：偶发"gate 已 ALL GREEN 且 `exit=0` 已打印但进程不退出"，
  按 **runner / process-exit hygiene observation** 归档 —— 不是 A3 的功能结果、不是产品 workaround、
  **不重开 NHI-1**（无新 native stack / 无新根因证据）；runner 侧已做进程表清空等待、每场景最多重试 1 次、
  功能结果与 hygiene 观察**分栏输出**。若出现稳定复现或新的 live native stack，独立开调查 slice。

## 2026-09-26 阶段 A / A2 F-2 Bootstrap Trigger（实现完成，BLOCKED）

- 新增 `--boot` managed gate（`Services/BootstrapTriggerGate.cs` + `Services/GateProbe.cs`）：
  7 个场景（Happy + 6 个 `EDITOR_BRIDGE_FAIL_*` 注入，每个注入独立进程——MSVC `getenv` 只读
  CRT 启动快照），覆盖 F2-01~F2-10：懒启动、唯一命名触发点 `EnsureRuntime()`、CloseProject
  不重建 GL（`glfwInit` 计数不增长）、终态无复活、legacy/EngineHost retention 差异各有断言。
- 修 3 个真实缺陷：bootstrap/open/play 失败不再以 `TargetInvocationException` 逃出 XAML handler
  （`HostOperationException` 保留 native `status`/`stage`/`diagnostic`）；pump 的 typed failure 不再被
  丢弃（停泵 + `Faulted` + 显式拒绝）；`Shutdown` 未完成时不再 `Destroy`（消除 late-free，
  修复前偶发 `0xC0000374`，修复后 50 次全绿）。
- 实测修正：`EDITOR_BRIDGE_FAIL_PLAY_BIND` 是**可恢复**失败（host 保持 `Ready`），第一版 gate 的
  假设被实测推翻后按事实改写。
- 1 处 HRC-1 断言按 F2-05 改写：`vm.OpenProject` 在 disposed host 上不再抛异常，改为
  返回 false + 显式上报（host 层 `EnsureRuntime()` 抛 `ObjectDisposedException` 的断言保留）。
- native 零改动：`test_hrc` 15/15、`test_bridge` 41/41、`test_scripting` 27/27（Debug + Release）；
  integrity gate I1+I2+I3 ALL GREEN（240 未变）；`--prodpath` / `--hrc2` / `--selftest` /
  `--gate1..3g` 断言全绿；fixture MD5 与 A1 一致；`git diff --check` exit 0。
- **未闭环（BLOCKER）**：gate 断言全绿、`exit=0` 已打印后，进程偶发不退出（`Environment.Exit(0)`
  之后卡住），并曾出现一次堆损坏。已排除 A2 改动、ASan、资源耗尽；只出现在**异常拆除**路径，
  属 native 范围。详见 `docs/Editor-Avalonia-HRC2-StageA-A2-Closure.md` §4。

## 2026-09-28 NHI-1 Closure B（root cause + 处置落地，A2 finding 关闭）

- **根因**（native stack，cdb 非侵入式 attach，进程已停滞 4 小时）：进程在 `main` 返回后的
  `LdrShutdownProcess` → FLS 回调阶段，被全局注入的 **NVIDIA ShadowPlay overlay
  （`nvspcap64.dll`）** 回调调用 `free()`，该 `free()` 被 **MSVC ASan 分配器**接管，
  进入 ASan TLS 初始化后在 `WaitOnAddress` 自旋死锁 → **进程永不完成退出**。
  **与 EngineHost / GLFW / OpenGL / teardown 无关**；native / teardown 产品代码零修改。
- **受控对比**（同一 native-only probe、同一异常拆除路径）：Debug+ASan **37/12,225 ≈ 0.30%**
  （OS 级残留进程计数为证）；Release 非 ASan **0/400**，退出阶段 78ms vs 600–900ms（约 15 倍）。
- **历史记录更正**：`findings=0/12,225`、批准批次 x80、针对性批次 x60 三项"无命中"结论**作废** ——
  根因是 harness 用 `Start-Process -PassThru` 的 `HasExited` 判定进程存活（PS 5.1 不可信），
  属 **harness 缺陷**，不得与产品 hang 混为一谈。
- **处置（裁决 ①+③，② 保留为分层策略）**：① runner 排除 `nvspcap64.dll` 注入
  （验证方式：被测进程模块列表不得出现该 DLL）；③ 永久登记为测试基础设施约束；
  ② 冻结证据分层：**Debug+ASan = 功能/内存/生命周期负例主要证据**，**Release 非 ASan =
  进程退出稳定性的独立证据**，Release **不替代** ASan。
- **三条仓库约定固化**：CJK `.ps1` 必须带 UTF-8 BOM；native C++ include 无 BOM UTF-8 头必须 `/utf-8`；
  **进程存活判定不得依赖 `Start-Process -PassThru`**。
- 工具沉淀：`tools/nhi1_stress.ps1`（managed 压测）、`tools/nhi1_native_teardown_probe/` +
  `tools/nhi1_native_probe.ps1`（native-only 8 场景 probe，**不进 CTest**）、DONE-then-stall detector。
- 状态：**NHI-1 CLOSED / Closure B**；**A2 `IMPLEMENTED / 7-7 GREEN`，finding → CLOSED**；
  native 计数与 I2 仍为 240（未变）；A8 继续 DEFERRED；下一步 Stage A / A3。

## 2026-09-27 NHI-1 立项（A2 之外的 native hang 调查）

- A2 负例跑批暴露的 native 退出缺陷已按裁决拆成独立切片 **NHI-1**（不重开 A2）：
  `docs/Editor-Avalonia-HRC2-NHI-1-Charter.md`（scope / 非目标 / 证据计划 / Closure A|B）
  与 `docs/Editor-Avalonia-HRC2-NHI-1-Log.md`（实测数据）。
- 新增调查工具 `tools/nhi1_stress.ps1`（**非门禁**）：场景化注入 + 逐次 exit/verdict +
  hang 时线程来源普查/托管栈/minidump + hang 后二次观察；退出码 2 = 至少捕获一次 hang。
- 已实测：hang 只出现在**异常拆除**路径，`exit=0` 已打印之后发生，存活时 35–37 线程 / ~700MB；
  `dotnet-stack` 无法对活进程应答（IPC pipe broken），minidump 显示 `Hosted Runtime: no`
  ⇒ 指向进程退出 / DLL detach 阶段。hang 率随负载在 0%–22% 波动。
- 已排除：A2 改动（恢复旧抛异常行为后同频率复现）、ASan（Release DLL 更频繁）、资源耗尽。
- 未闭环：本机无任何 native 调试器（cdb/windbg/procdump 均缺，`dotnet-dump` 无 `~*k`），
  因此 native 栈待取；NHI-1.4（native-only 最小复现，不需调试器）为当前性价比最高的下一步。
- **NHI-1 是 A2 → 下一阶段之间的 blocker**；A8 继续 DEFERRED。native 计数与 I2 仍为 240（未变）。

## 2026-09-28 HRC-2 Closure（证据链汇总 + 最终判定，零代码改动）

- 收口文档：`docs/Editor-Avalonia-HRC2-Closure.md`。**纯文档 + 证据复核 slice，零代码改动**。
- 证据链汇总：A0–A3 native / A4 / A6 / A7-0~2 / Stage A A1（F-1）/ A2（F-2，7-7）/
  A3（4-4）/ NHI-1（Closure B）→ **HRC-2 CLOSED**。
- **HRC-G2P-01~07 判为 GREEN**，且每项都挂**已执行的用例**而非叙述性结论：
  G2P-01（FBO ready/complete/identity/readback）、G2P-02（`CreationThreadContract` wrong-thread →
  `WRONG_THREAD`/`STAGE_PLATFORM`；`owner_thread_id` 同一性）、G2P-03
  （`SinglePumpOwnerAndLifecycleRegression` + A2 唯一 pump）、G2P-04（`sizeof(RuntimeResult)==76` +
  32-call routing + typed status/stage）、G2P-05（三处 rollback + A2 六注入场景 no half-init）、
  G2P-06（`SessionProofAndExactlyOnceStop` / `CloseDetachesBeforeStopCallback` / teardown 可观测）、
  G2P-07（`GlobalStateReturnsToBaseline` + `RuntimeSeamProofThreeCycles` 三周期回 baseline）。
  **G2P-08 仍 DEFERRED**（A8，无真实 Linux/Xvfb；计划已判定不阻塞）。
  G2P-01~07 的 GREEN 范围明确限定为 **Windows hidden-GLFW 单机**，不得当作跨平台 receipt。
- HRC-2-A readiness：`CONDITIONAL PASS` → **`PASS`**（F-1/F-2 两个 blocking finding 均已 CLOSED）。
- 收口当日一手复核：`test_hrc` **15/15**、`test_bridge` **41/41**、`test_scripting` **27/27**
  （Debug + Release 各跑一次）；integrity gate I1+I2+I3 ALL GREEN（**240 未变**）；`--uistate` 4-4；
  `--prodpath` / `--boot` / `--hrc1` / `--hrc2` / `--selftest` / Gate 3a / 3g 全绿；
  native 源码改动 **0**；residual **0**；`git diff --check` exit 0。
- 两条复现注记（避免下次误判为回归）：① Debug native 套件需
  `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1`（NHI-1 runner 要求，integrity gate 已内置），
  缺失时在 `interception_win.cpp:193` CHECK fail，**不是**测试失败；② Release 全量构建在
  `tests/test_ecs/ComponentLifecycleTest.cpp` 编译失败，该文件自 2026-07-16（`4d0db67`）未改动，
  与 HRC-2 无关，相关 Release target（`EngineCore` / `EditorBridge`）exit 0。
- 冻结边界三条（后续 slice 不得静默改写）：N3 的 evidence boundary（`Failed` 分支无生产证据）、
  process-exit hygiene observation（不重开 NHI-1、非产品 workaround、不计入功能 verdict）、
  ABI 冻结（`(1<<16)|1` / 24 个 `EngineHost_*` 导出 / 27 个具体 op / `RuntimeResult` 76B）。
- 能力归属：viewport 的**渲染真实性**（render target 呈现、FBO readback 与 UI 呈现一致性、
  viewport 交互与相机同步）**移交 HRC-3 Viewport Reality**，本轮未打开；A8 继续 DEFERRED。

---

## D3 Step 1：Pick Reality Audit + Contract Freeze（2026-09-29，**零代码**）

**状态**：`D3 STEP-1 CLOSED` —— D3-C1~D3-C8 已冻结；**D3 Step 2 未批准**。

文档：`docs/Editor-Avalonia-HRC3-D3-Pick-Contract-Freeze.md`

### 核心结论

- **D3 不是"接线"，而是从无到有。** 树内有 **4 套** pick 实现，**全部 0 生产引用**：
  `PickingFBO`（GL R32UI）· `OpenGLFramebuffer::m_PickTexture`（GL R32I / MRT）·
  `IDBufferPass`（**RHI** R32_UInt + CPU readback）·
  `ViewportPanel::HandleMousePicking`（legacy ImGui FBO，NHI-1 禁止路径）。
- **无 frame 绑定**：三条 readback 都不携带/校验 `frame_id`，即被禁止的
  「最近一次 pick buffer」语义。
- **ID 格式四份声明不一致**；**sentinel 有 0 与 -1 的活冲突**（`lit.frag:10` 的
  `u_EntityID = -1` 会写入 0xFFFFFFFF）；**y-flip 有 `height-1-y` 与 `height-y` 两种**
  （差一行像素）；**越界处理一个 clamp 一个拒绝**。
- **C++ 从不设置 `u_EntityID`、从不加载 `pick.frag`** —— shader 侧确实会写 ID，
  但**没有任何生产 render 路径真正写入**。

### 冻结的 8 条 contract

| Contract | 冻结内容 |
|---|---|
| D3-C1 | pick 真值 = 当前 presented/runtime frame 的 ID 表面（与 P2 同一次 render） |
| D3-C2 | sentinel 恒为 `0`（no-hit ≠ failure）；entity 编码 **≥ 1**；采 **`out int` + R32I/GL_INT**（4 个 shader + 现成 attachment 已一致）；禁 RGB 24-bit `EncodeID` |
| D3-C3 | top-left 原点，单次 y-flip `py = height-1-y`；越界**显式拒绝**，**禁 clamp**；固定 800×600 1:1 |
| D3-C4 | result 必带 `pickFrameId`；stale 显式区分；**禁**「最近一次 pick buffer」 |
| D3-C5 | 复用 P2 **唯一 pump**；runtime thread 执行，UI thread 无 GL；不建 pick thread / UI-side GL path |
| D3-C6 | typed result + 7 类 failure；**readback 失败 ≠ no-hit**（须分离） |
| D3-C7 | legacy 路径仍禁止（含 `EditorSession::ViewportPick` 的 `-1` stub 不得被接上） |
| D3-C8 | D4 resize 保持独立；不引入 `m_ResolutionScale` |

### 本 Step 边界

**零代码**：未修改任何 runtime / native / ABI / I2；
`test_bridge` 仍 **44/44**，**I2 仍 243**，ABI 仍 **v1.2**，
`test_hrc` 15/15，`test_scripting` 27/27；
P2 standing runner（`tools/run_p2_gates.ps1`）未受影响、仍在回归面内。

### Step 2 未决项（进入实现前必须先答）

U1 选哪套实现 · U2 谁真正驱动 ID render pass（**新增工作量，非接线**）·
U3 `location=1` 与 deferred 颜色的冲突隔离 · U4 `lit.frag` 的 `-1` 默认值修正 ·
U5 entity identity 沿用 `GameObject::ID` 还是独立 pick handle。

---

## D3 Step 2：Pick Contract Resolution（2026-09-29，**docs-only**）

**状态**：`D3 STEP-2 CLOSED` —— U1~U5 全部解决；**D3 Step 3（实现）未批准**。

文档：`docs/Editor-Avalonia-HRC3-D3-Pick-Contract-Resolution.md`

### U1~U5 解决结果

| 项 | 冻结结论 | 决定性证据 |
|---|---|---|
| U1 | 采用 **`OpenGLFramebuffer::m_PickTexture`（R32I）**，弃 `PickingFBO`(R32UI) / `IDBufferPass`(RHI) / legacy panel；**不造第四种格式** | attachment 已在 production framebuffer 上；`OpenGLFramebuffer.cpp:130-135` |
| U2 | Driver = **`Application::PumpOneFrame(float32 dt)`**（`Application.cpp:140`），复用 P2 `RuntimeTick` 同一入口；不建第二 pump / pick thread | managed 经 `EngineHost_PumpOneFrame` 驱动 |
| U3 | **dedicated pick pass**（`out int EntityID @ location=1`）；不与 deferred 共用 fragment-output contract；**不为 D3 改 `lit.frag`** | `deferred_geom.frag:20` 同占 location=1 |
| U4 | **reject, never clamp**；只定义一个 Y 公式，**常数待 executable assertion 后再冻结**；不兼容两个差一像素的旧实现 | `PickingFBO.cpp:140` vs `ViewportPanel.cpp:398` |
| U5 | **复用 `GameObject::ID`**，不引入 `PickHandle`、不造第二身份系统 | `GetNextID()` 从 1 单调；`Scene::FindByID` public；`m_ID` 注释本就写明"编辑器 Picking 相关" |

### 冻结后的 pick contract

```text
Pick target:    R32I / existing m_PickTexture (GL_COLOR_ATTACHMENT1)
Sentinel:       -1        (background / no-hit)
Valid ID:       >= 1
Reserved:       0         (invalid / unset)
Driver:         Application::PumpOneFrame (existing runtime pump)
Thread:         runtime thread
Frame identity: result carries owning frame_id
Shader:         dedicated pick pass
Y semantics:    one explicit formula (常数待定)
OOB:            reject, never clamp
```

### ⚠ 修正 Step 1 的 D3-C2 / D3-C3

- **D3-C2 sentinel 由 `0` 改为 `-1`**（`0` 改为 reserved）。理由：signed `-1` 有**三方证据自然收敛** —— `out int` shader · `R32I` attachment · legacy 已有 `ClearBufferiv(-1)`（`ViewportPanel.cpp:188-189`）；且 `>= 1` valid 区间与 `s_NextID` 从 1 开始天然对齐。
- **D3-C3 的 Y 公式常数撤回**：Step 1 写的 `height-1-y` 降级为**候选**，在 known-pixel executable assertion 建立前**不冻结**。
- **U4（`lit.frag` 的 `u_EntityID = -1`）不再是待修正项**：sentinel 定为 `-1` 后该默认值恰好等于 no-hit，**不构成冲突**，D3 无需为此改 `lit.frag`。

### 废止条款（实现前必须执行）

1. `ReadPixelID` 的「0 = failure/no-hit」语义**废止** → 结构化 result。
2. **`OpenGLFramebuffer.cpp:247` 的 `std::max(0, pixelValue)` 必须移除** —— 它会把 `-1` clamp 成 0，直接摧毁冻结的 sentinel。
3. pick pass 必须**显式 clear attachment 1 到 `-1`**，不得依赖普通 `glClear`（会清成 0 ⇒ 未命中变**假命中**）。

### 本 slice 边界

**零代码**：未修改任何 runtime / native / shader / managed / ABI / I2；
`test_bridge` 仍 **44/44**，**I2 仍 243**（指 Step 2 当时；见下方 Step 3/3.1 更新），
ABI 仍 **v1.2**，`test_hrc` 15/15，
`test_scripting` 27/27；P2 standing runner 不受影响。**D3 Step 3 未批准。**

---

## D3 Step 3 + 3.1：Production Pick Implementation + Native Known-Pixel Proof（2026-09-29）

文档：`docs/Editor-Avalonia-HRC3-D3-Known-Pixel-Proof.md`

**状态**：`D3 STEP-3.1 CLOSED`（native 空间语义已由可执行证据闭合）
· **D3 Step 4 / ABI mini-contract 未批准**

### 已实现（native，无 ABI）

| 项 | 内容 |
|---|---|
| ID 通道 | `RenderCommand` 增量字段 `entityId`；`SpriteComponent::CollectRenderCommands` 用**既有** `GetOwner()` 填入 `owner->GetID()`（无需改接口，Step 1 担心的 back-pointer 其实已存在） |
| pick shader | 新增 `assets/shaders/pick.vert`（几何与 `sprite_batch.vert` 逐字等价，防止 color/pick 错位） |
| pick pass | 在 `RenderProductionFrame` 内、与 color pass **同一次 render**：共用同一批 `RenderCommand`、同一 camera 指针、同一 `ISpriteBatch`，经 `ToSpriteData()` 转发到**同一份** `ExtractSpriteData`（刻意不复制一份） |
| sentinel | attachment 1 显式 `glClearBufferiv(-1)`；`entityId == 0` 写 `-1`，**绝不**写 0 |
| readback | `ReadPixelID` **移除**；新增 `ReadPickPixel` → `PickReadback{Hit/NoHit/InvalidCoordinate/OutsideViewport/NotReady}`，**无** `max(0,…)`，越界**拒绝**不 clamp |

### 夹具查出的三个真实缺陷

1. **`m_GL` 在 production 恒为 null**（只由依赖非 headless 窗口的 `InitViewportFBO()` 赋值）
   → pick pass **静默跳过**。修复：从传入的 `IRenderContext&` 解析 GL，失败时显式报错。
2. **坐标公式实测为 `H - y`，比 D3-C3 差一行** —— canonical row 0 不可达、
   每个采样系统性上移一行。修复为 `H - 1 - y`；结论由 known-pixel 探针**实测**得出。
3. **每帧无条件跑 pick pass 会让 production 挂起** —— P2 gate 双双 timeout，
   直跑 `--p2frame` 超过一小时未返回。**根因未定位**；不做任何重试/延迟掩盖。
   改为**显式 opt-in、默认关闭**，这也正是 U2 冻结语义的本意
   （pick pass 仅当存在 pending request，而请求通道尚未存在）。

### known-pixel 证据（`test_pick`，8/8，Debug + Release）

known pixel → **精确** `GameObject::ID` · 背景 → 原始 GL 读取**精确 -1** ·
坐标公式判别 · 越界/非有限/NotReady · 0 永不为有效 hit · pick pass 真实写入。
夹具**不链接 EditorBridge**（native-only），且已知像素取自 **color attachment 实测**，
不复制投影矩阵。

### I2 基线（显式重算）

| 项 | 变化 |
|---|---|
| `test_bridge` | 42 → **44**（陈旧行，本次一并修正） |
| `test_pick` | **新增 8** |
| **I2 合计** | 243 → **251** |

### 冻结基线

`test_pick` **8/8**（Debug+Release）· `test_bridge` **44/44**（Debug+Release）·
`test_hrc` 15/15 · `test_scripting` 27/27 · **ABI v1.2 不变**（无新增 export/op/packet）·
**I2 251** · P2 standing runner **ALL GREEN**，`nonClear=7296` 不变 ·
D4 Resize 未进入 · A8 未进入。

### 未决

1. **挂起根因未定位** ⇒ 在解释清楚前 pick pass 不得在 production 默认开启。
2. **D3-C1 的 EngineHost 侧证明未完成** —— 夹具自建 frame，不是 EngineHost 的
   production frame；managed→native 请求链路需先冻结 **D3 ABI mini-contract**。

---

## D3-H7 / D3-H8：Pick transport 根因定位与死锁修复（2026-09-29）

**状态**：`D3-H7 CLOSED`（根因已定位）· `D3-H8` 死锁**已修复**，但暴露 3 个新语义缺陷
⇒ **D3-Native v1.3 仍 RED / NOT CLOSED**

### ⚠ 撤回 D3-H2 的错误结论（必须保留在证据链中）

D3-H2 曾写下「`FillRejectedResult` — verified not to take `m_Mutex`」。**该结论错误。**

> H2 只检查了 `FillRejectedResult` 的**直接函数体**，漏掉了它的**传递性调用者**：
> `ResetResult`（L162）与 `SetDiagnostic`（L147）**各自 acquire `m_Mutex`**。
> D3-H7 确立了真正的重入路径。

这个误判是 `m_Mutex` 假设被误判为"已排除"数轮的直接原因：直接 PICK 锁点确实作用域正确，
但**间接**路径被漏检。**教训：检查"某函数是否加锁"必须包含其调用图，不能只看自身函数体。**

### 根因（D3-H7，source-first，未加任何 instrumentation 即定位）

```text
FetchPickResult              持有 m_Mutex                    (L2177)
  └─ rejectAt → FillRejectedResult
       ├─ ResetResult(result)  → lock_guard(m_Mutex)         (L162)
       └─ SetDiagnostic(...)   → lock_guard(m_Mutex)         (L147)
```

对非递归 `std::mutex` 的同线程重入 = UB / 自死锁。
pre-service `Fetch(PICK)` 恰好是唯一走到"持锁调用 `FillRejectedResult`"的路径 ——
这解释了 H6 的全部现象：只旁路该 Fetch，帧 2 立即通过。

### D3-H8 修复形状（**未**改 `recursive_mutex`）

三处**同一所有权违规**全部按「锁内只做状态决定，解锁后再 mutation」修复：

| 站点 | 性质 |
|---|---|
| `FetchPickResult` no-result 拒绝 | D3 复现路径 |
| `EnqueuePendingEdit` 命令拒绝 | **既有**潜在缺陷（host 不接受命令时） |
| `EnqueuePickRequest` `PICK_BUSY` 拒绝 | D3 新增面 |

> 不变量：**持有 `m_Mutex` 的函数不得调用任何可能 acquire 同一 mutex 的函数。**
> 绝不用 `recursive_mutex` 掩盖重叠所有权边界。

修复效果：abort（exit 3）消失，`D3PickRequestPumpFetchHappyPath` **转绿**。

### 死锁解除后暴露的 3 个新语义缺陷（**尚未修复**）

| # | 缺陷 | 证据 |
|---|---|---|
| 1 | outstanding 判定漏掉「已受理、仍在 pending 队列、尚未 dispatch」这一状态 | `idB` 被受理（2）而非 `PICK_BUSY(12)`；`Fetch` 返回 B 的 `request_id=2` 而非 A 的 `1` |
| 2 | `EngineHost_Stop` **不**作废 pending pick 状态 | Stop 后 `Fetch` 仍**成功**返回（`status=0`），而非 `INVALID_STATE(2)`+`STAGE_SHUTDOWN(10)` |
| 3 | reopen 路径同族（`Close`+reopen 后旧结果语义） | `D3PickOldRequestCannotReappearAfterReopen` 失败 |

即：`EnqueuePickRequest` 的 outstanding 判定只覆盖 `m_PickFrameRequest.active`（已 dispatch）
与 `m_PickResultPresent`（已产出），**缺第三态**；且 `Stop` 路径未接入
`AbandonPendingEdits`/`InvalidatePickResult`。

### 本 slice 边界

未改 ABI 布局/版本（仍 **v1.3**）、`RuntimeResult`（仍 **76B**）、pending queue 语义、
pick rendering、P2、D3-Managed/UI、D4、A8。无 retry / timeout / watchdog / recursive_mutex。
**I2 仍 251**（durable transport inventory 未最终确定）。

---

## D3-H1：Pick-Pass Hang Localization（2026-09-29）→ **更正上一轮结论**

文档：本节（诊断设施已按批准删除，见 §删除）

**状态**：`D3-H1 CLOSED` —— 结论是 **D3 pick pass 不存在已证明的 hang**。

### ⚠ 对上一轮结论的更正（保留在证据链中，不得抹去）

上一轮报告的「每帧无条件跑 pick pass 会让 production 挂起」是**错误归因**。
真实原因：当时只重建了 `EngineCore`，`editor_avalonia/bin/.../EditorBridge.dll`
仍是旧时间戳 —— **应用根本没有执行任何 D3 pick code**。那次"挂起"发生在
一个不含 pick pass 的二进制上。

伴随信号当时也被误读：某次 build 步骤返回 `exit=1`，而随后读到的
`8/8 PASSED` 来自**未重新链接的旧二进制**。

### 有效证据（artifact 新鲜度已显式验证）

| Case | 结果 |
|---|---|
| **A** 无 pick request（diag off） | gate **ALL GREEN**，22s（基线 ~22–25s），`nonClear=7296` 不变，**0 marker** |
| **B** pick pass 开启 | **进程自行退出，code 0，无挂起**。24 个 marker：`P0→P1→P2→P3→P4→P5→P7→P8` 按序完整，`seq=1,2,3` 连续三帧 |
| **P6** readback（app 不可达，native 覆盖） | 2 个 marker，无 stall |

结论：连续三帧全部 phase 完整返回，**既不存在 first-use 初始化问题，
也不存在 second-use / 资源复用问题**。无需进入更多 stress。

### process-exit 现象的定性（不再重开）

> **Known infrastructure observation, not a D3 defect and not a D3 blocker.**

- 已在 HRC-2 Closure §6 记录为 process-exit hygiene observation；
- 本轮证明**不是** pick pass；
- 当前 pick-on 三帧真实执行且 clean；
- 无新的 native stack、无新的 pick-specific evidence。

只有出现新的、**可归因到当前 D3 path** 的 process-exit evidence 才另开独立 slice。
**不因为它曾经出现过，就在 D3 之下再做一次泛化调查。**

### 证据纪律修正（后续 D3 gate 的 preflight 要求）

> **native build 成功 ≠ production managed process 已加载最新 native/managed pair。**

任何 D3 gate 在采信结果前，必须先**证明被测 managed/native artifact 是当前构建**
（比较 artifact 与最新 source 时间戳 / 显式重链），**不得**只凭某个 build 命令的
历史 exit code。这是**证据纪律**，**不**改成产品机制。

### 删除（按批准）

`HRC3_D3_PICK_DIAG` 及其 marker/trace 设施已**整体删除**（问题已回答、
无产品用途、保留会制造永久诊断面与未来误判）。清理后验证：
引用残留 **0** · header 已删 · trace 文件已删 · `test_pick` 8/8（Debug+Release）·
`test_bridge` 44/44 · P2 standing **ALL GREEN** · ABI **v1.2** · **I2 251** ·
`diff --check` 0 · residual 0。

> pick pass 保持 **default-off** 的理由是 **contract**：U2 冻结「pick-ID pass 仅当存在
> pending request 时运行」，而当前**尚无 request channel**。**不是**因为挂起。

---

## D3 ABI Contract Resolution：四项未决冻结（2026-09-29，**docs-only**）

文档：`docs/Editor-Avalonia-HRC3-D3-ABI-Contract-Resolution.md`

**状态**：`D3 ABI RESOLUTION CLOSED` · **v1.3 implementation 未批准**
**零代码**：ABI 仍 **v1.2**，**I2 仍 251**。

### 编号复核（直接读当前 `capi.h`，非复制旧表）

`GET_PRESENTATION_FRAME = 28`（已是最后一个）· `OP_COUNT = 29`
⇒ **`PICK = 29`**，`OP_COUNT` **29 → 30**。
STATUS 实际枚举到 **11**（`NO_NEW_FRAME`）· STAGE 实际枚举到 **11**（`EDIT`）。

### 四项冻结

| 项 | 冻结结论 |
|---|---|
| **PICK_BUSY** | `status = 12`（**append-only**，接在 `NO_NEW_FRAME(11)` 后）；`stage = ENGINE_HOST_STAGE_EDIT (11)` —— **既有 stage，不新增**。busy 的成因就是既有 pending/edit 队列已占用，语义吻合。`EDIT_ACCEPTED(10)` 是受理语义相反、`INVALID_STATE(2)` 会把"队列忙"与"状态坏"混同，故 status 必须新增 |
| **teardown 失效** | 任何 `Stop`/`Close`/terminal teardown 使全部 pending request **立即失效**，之后 `Fetch` **不得**返回旧结果。反例序列 `request N → Stop → reopen → Fetch` 被明确禁止。wire 语义：`status = INVALID_STATE (2)`（既有）+ `stage = SHUTDOWN (10)`（既有），**且不产生任何 packet** —— 失效是"没有结果"，不是"有一个失败结果"；返回 packet 会绕过 D3-C6 的区分 |
| **managed pairing** | `packet.request_id` 必须等于 outstanding `operation_id`，不匹配即**丢弃**。三序列冻结：① 正常配对 ② `A` 后 `B` 返回 `PICK_BUSY`（**B 从未入队**），`Fetch` 返回 **A**，managed 不得等待 B ③ teardown 后 `Fetch(A)` 无 packet，managed 丢弃 A。禁止缓存"最近结果"复用 |
| **artifact freshness** | 升级为 **gate contract**（见下节），**fail-closed** |

---

## D3 Gate Contract · Artifact Freshness（**采信前提**）

> 在任何 D3 gate 结果被采信前，必须**证明**被测 managed bridge 与 native DLL
> 是**当前构建的配对 artifact**。

**可接受的证明（满足其一）**：

1. **时间戳** —— native DLL 的 mtime 晚于所有相关 native source/build input；
   managed assembly 晚于所有相关 managed source。
2. **显式 build receipt** —— 本次 gate 运行前完成一次可验证的 relink/build receipt。
3. **artifact identity** —— gate **自行打印** artifact identity/version 并与本次
   build receipt 匹配。

**不再接受**：

- ❌ 只看某个 build 命令的**历史 exit code**；
- ❌ 只看 managed 侧能启动（能启动不代表加载的是最新 native）。

**落地要求**：D3 gate runner **必须**在跑场景前执行 freshness 检查，
**不通过则拒绝运行（fail-closed）**，而不是打 warning 继续。

> 依据：本轮曾因 stale `EditorBridge.dll` 误判 —— 应用根本没执行 D3 pick code，
> 却"复现"出一个不存在的 hang。

---

文档：`docs/Editor-Avalonia-HRC3-D3-ABI-Mini-Contract.md`

**状态**：`D3 ABI CONTRACT FROZEN`（transport 形状已冻结；**实现未批准**）
**零代码**：ABI 仍 **v1.2**，**I2 仍 251**。

### 决定性既有事实

`capi.h:586-589` 规定 GL op 走 `EngineHost_Dispatch` 时返回 pending edit id、
status = `EDIT_ACCEPTED`，**仅表示已受理、不表示已执行**，执行发生在下一次
`PumpOneFrame` 的 current-context 窗口内。

⇒ **UI 线程不可能同步拿到 pick 结果**；request 与 result 天然**跨帧**。

### 冻结形状

- **两段式**，与 P2 同构：`Dispatch(PICK)` → `PumpOneFrame` → `Fetch(PICK_RESULT)`。
- **不新增函数族**，只新增 op 编号与 result 状态码。
- `PickResultPacket` **定长 32B**：`struct_size`(4) + `status`(4) + `frame_id`(8) +
  `request_id`(8) + `entity_id`(4) + `reserved`(4)。
  pick 只需**一个像素** readback，**不**把 ID 表面搬过 ABI。
- `frame_id` **必带**（D3-C4）；`request_id` **必需**（因跨帧关联，复用既有 pending id）。
- pending 容量 **1**；重复请求返回明确 busy，**不静默覆盖**。
- 状态集：`Hit` / `NoHit` / `OutsideViewport` / `InvalidCoordinate` / `NotReady` / `Fault`
  —— `NoHit`（查询有效、无实体）与 `OutsideViewport`（查询无效）**必须结构化区分**。
- **ABI v1.2 → v1.3**（依 P2 先例：加 op + 新 status 即升 minor）。
  **`RuntimeResult` 不变，仍 76B。**

---

## D3-H8 semantic closure（native 侧）

**状态**：`D3-H8 semantic fix CLOSED`；`D3-Native v1.3 CLOSED`。
整体 D3 仍 **OPEN**（managed / UI 未完成）。

**验证结果**：`test_pick_transport` **6/6 GREEN，Debug + Release，exit 0**
（含 H3 discriminator）。无回归：`test_bridge` 44/44、`test_pick` 8/8、
`test_hrc` 15/15、`test_scripting` 27/27。Debug build 0 error，`diff --check` 0，残留进程 0。
freshness preflight PASS（dll > native source；exe > source 且 > dll）。

### H8 根因（已证明成立）

`FetchPickResult()` 持 `m_Mutex` 时调用 `FillRejectedResult()`，后者经
`ResetResult()` / `SetDiagnostic()` 再次 acquire 同一**非递归** mutex → UB / 自死锁。
修法为**锁内只决定状态、解锁后再 mutation**（`FetchPickResult`、
`EnqueuePendingEdit` 的 command reject、`EnqueuePickRequest` 的 `PICK_BUSY`），
**未引入 `recursive_mutex`**。死锁解除后 `D3PickRequestPumpFetchHappyPath` 由 abort 转 GREEN。

> 附注：H2 曾误判"`FillRejectedResult` 不加锁"，该结论已撤回。
> 教训：判断"某函数是否加锁"必须看**调用图**，不能只看函数体。

### 三项语义修复（native 内部状态机，**非 ABI 改动**）

- **H8-1 accepted-but-queued capacity**：新增 `m_PickOutstandingRequestId` +
  `m_PickOutstandingGeneration` 作为**唯一** outstanding 判据，覆盖
  `Idle` / `Queued` / `Dispatched` 三态。受理时即在 `EnqueuePendingEdit` 内占位
  （A 一旦受理就占据 capacity=1，**不等 dispatch**），`FetchPickResult` 消费后释放。
  此前只看 `m_PickFrameRequest.active`（已 dispatch）与 `m_PickResultPresent`（已产出），
  **漏掉「已受理、仍在 pending 队列、尚未 dispatch」**这一态，导致 B 被错误受理。
- **H8-2 Stop invalidation**：`InvalidatePickResult()` 扩展为作废**全部** pick 状态
  （含未 dispatch 的那一个），并接入 `EngineHost_Stop()`。
  沿用**已冻结的** invalidation 机制，未另造第二套 stale-event 机制。
- **H8-3 reopen generation**：沿用已冻结 generation 契约；旧 generation 的
  outstanding 与已作废结果在下次 `EnqueuePickRequest` 时丢弃，旧 request 不可复现。

### 两个由**测试**（而非 review）发现的实现错误

1. **invalidated 分支求值顺序**：`InvalidatePickResult()` 刻意保留
   `m_PickResultPresent`（用以区分"被 teardown 作废"与"本来就没有"），
   但 `FetchPickResult` **先**判 `m_PickResultPresent`，使 invalidated 分支
   对它本该处理的场景**永远不可达** —— 表现为 `Stop()` 之后 `Fetch` 仍成功返回旧
   packet。改为 `m_PickResultInvalidated` **先判**。
2. **已作废结果错误占用 capacity**：Close/reopen 之后新一代第一个 request 得到
   `newId = -1`（被判 `PICK_BUSY`），因为"已作废但 present"的结果仍计入 busy。
   已作废结果不再占 capacity。

> 两者与 H2 同族：**局部不变量看似成立，却被求值顺序/跨状态一致性击穿**，
> 且都是测试而非 review 发现的。

### `m_Mutex` 与既有 `m_CallbackMutex` 的区分（避免误判）

- `m_Mutex`（`EngineHost.h:282`）= **plain `std::mutex`**，即 H8 死锁的锁。
  故 H8 死锁是真实的，且修复**未**将其改为 recursive。
- `m_CallbackMutex`（`EngineHost.h:283`）= **既有 `std::recursive_mutex`**，
  仅用于 callback 重入边界（`EngineHost.cpp:1627/1640`），**与 pick/H8 路径无关**。
  全仓 7 处 `recursive_mutex` 命中 = 该 callback mutex + 注释，**非本次引入**。

### P2 standing 当前状态

> **BLOCKED BY COORDINATED ABI TRANSITION / INADMISSIBLE**

- **不是** P2 功能回归，**不是** GREEN，**不**改 `run_p2_gates.ps1`，
  **不**让 M1 接受 v1.2/v1.3 混合，**不**为绿灯把 native 降回 v1.2。
- 两个独立成因，均为结构性：
  1. **native v1.3 已通过，managed 仍 v1.2** → M1 lockstep 必然失败；
  2. `editor_avalonia/bin/.../EditorBridge.dll` **stale**（artifact freshness 问题）。
- 该次运行**不产生任何功能 verdict**。P2 standing 需在
  **D3-Managed v1.3 完成后**建立 native v1.3 + managed v1.3 fresh pair，再恢复 GREEN。

### I2

**I2 仍为 251。** 257（251 + 6 个 `test_pick_transport`）仅为
**已发现的 durable-test inventory 候选值，尚未 baselined**。
Baseline 更新留到 D3-Native + D3-Managed durable inventory 最终稳定后**一次性**处理。

---

## D3-Managed v1.3：CLOSED（semantic / contract closure）

**状态**：`D3-Managed v1.3 CLOSED`（语义/契约层面）。

**验收证据**：`M1 ~ M8` 在 **Debug 与 Release 双配置 ALL GREEN**，含
artifact freshness、v1.3 lockstep、packet 布局、request/result 配对、
busy 行为、stop invalidation、generation 隔离、frame 绑定。

| Gate | 内容 | 证据 |
| --- | --- | --- |
| M8-A | artifact identity（**SHA256**，非 timestamp） | build/staged `EditorBridge.dll` 字节相同 |
| M1 | ABI v1.3 lockstep | native = managed = `0x00010003` |
| M2 | op/status/packet 镜像 | `PICK=29`、`opCount=30`、`PICK_BUSY=12`、packet 32B、**Marshal 实测 offset 0/4/8/16/24/28**、`RuntimeResult` 76B |
| M3 | request_id 配对 | `operation_id == request_id` |
| M4 | `PICK_BUSY` 保留 outstanding | A 存活，未被 B 替换 |
| M5 | Stop invalidation 无 packet | `INVALID_STATE` + `STAGE_SHUTDOWN` |
| M6 | reopen generation 隔离 | 旧 request 不复现，新代可配对 |
| M7 | frame_id 绑定 | `frame_id=6` 非 0 且与结果绑定 |
| M8-B | loaded ABI identity | 实际加载的 DLL 报 `0x00010003` |

> M2 以 `Marshal.SizeOf` / `Marshal.OffsetOf` 的**运行时实测值**为权威，
> 不以 `StructLayout` 属性为权威。`PickResultPacket` 用 `Pack = 1`，
> 因 native 侧位于 `#pragma pack(push, 1)` 内。

### Gate 自身发现并修正的三个实现错误（均由测试发现，非 review）

1. **`if (code == 0) return NoResult`** —— `FetchPickResult` 成功时返回 **0**
   （与 `FetchPresentationFrame` 返回字节数不同），该判断会丢弃**每一个**成功结果。
2. **先判 `code < 0` 再判 status** —— native `rejectAt` 返回 **-1** 的同时**写入结构化
   status**，故 `-1` 不是失败信号；合法的"无 packet"结局（未产出 / teardown 作废）被误抛。
3. **runner 工作目录**误设为 `bin` 而非 repo root，导致 manifest 解析失败。

> 三者同族：**把相邻函数的约定当成本函数的约定**。与 H2 误判、H8 求值顺序缺陷一致。
> M2 是唯一建立在实测值而非"读 neighbouring function"上的断言。

### Staging S1–S3：CLOSED（build/deployment hygiene，独立 slice）

| 项 | Debug | Release |
| --- | --- | --- |
| config-aware source（`$(Configuration)`，无硬编码 `Debug`） | ✅ | ✅ |
| 普通增量 build 正确部署（**不需 `-t:Rebuild`**） | ✅ | ✅ |
| bin DLL 被更新 | ✅ | ✅ |
| `SHA256(build) == SHA256(bin)` | ✅ | ✅ |
| loaded ABI = v1.3 | ✅ | ✅ |
| 无手工 copy / 无 retry | ✅ | ✅ |

原缺陷：`..\build\bridge\Debug\` 硬编码 + `PreserveNewest` ⇒ 增量 build 留下陈旧 bin DLL
（且无任何警告），Release 实际部署 **Debug** DLL。
S2 以"把 staged 时间戳改旧 3 小时后跑普通 `dotnet build`"证明：staged 被重新部署且 hash 一致。
`M8-A` **保留**为最终安全网。

`OpenAL32.dll` 同类硬编码缺陷**刻意未改**（避免一个 slice 同时动两件事），单独 deferred。

---

## D3-Managed process-exit hygiene：OPEN（**外部 framework 发现**，非 D3）

**状态**：`OPEN`。**不影响** D3-Managed 的 semantic/contract closure。

**现象（统一措辞）**：

> **Nondeterministic Avalonia/Win32 shutdown-path hang observed during `Environment.Exit`,
> positively localized by dump to the render/compositor path ending in
> `NonPumpingSyncContext.Wait → WaitForMultipleObjectsEx`.**
>
> `Environment.Exit` is entered from `TopLevel.OnOpened` and re-enters the Win32 window
> procedure through a **reverse P/Invoke**. The observed blocking chain proceeds through
> `WM_PAINT`/compositor rendering and ultimately blocks in
> `NonPumpingSyncContext.Wait → WaitForMultipleObjectsEx`.

**不**将 "race" 写为已确立的 root cause：dump 证明的是**阻塞路径**，
**不是**该 wait 为何在部分运行中永久不被满足。

进程 linger，**非 Release-only —— Debug 也会（间歇性）发生**。

观测（同一 binary pair、期间无任何代码改动）：

| 配置 | 观测计数 |
| --- | --- |
| Release | **3 / 3** linger |
| Debug | **1 / 5** linger（其余正常退出） |

> 以上为**已观测计数**，样本量小；**不**作为稳定概率或发生率结论。
> ⚠ 早期记录曾写"Release-only；Debug 正常退出"，**该说法已被后续观测否证**。
> Debug 在一次验证运行中同样 exit=3，随后 3 次又全部 exit=0，**binary 未变、代码未改**。
> 因此该现象**不是配置特有的**；这进一步削弱任何"Release 特有"的解释。

**归属（由 624 MB full dump 正确定位，dump 于 linger 期间采集，末行日志 `d3managed exit=0`）**：

```
System.Environment.Exit(Int32)
  → [native] → ILStubClass.IL_STUB_ReversePInvoke        ← reverse P/Invoke 重入，非注册回调
    → Avalonia.Win32.WindowImpl.WndProcMessageHandler
      → WndProc → AppWndProc
        → Avalonia.Controls.TopLevel.HandlePaint
          → CompositingRenderer.Paint
            → Avalonia.Media.MediaContext.SyncCommit
              → MediaContext.SyncWaitCompositorBatch
                → Task.Wait → ManualResetEventSlim.Wait → Monitor.Wait
                  → Avalonia.Threading.NonPumpingSyncContext.Wait
                    → NonPumpingWaitHelperImpl.Wait
                      → UnmanagedMethods.WaitForMultipleObjectsEx   ← 阻塞于此
```

**栈上没有任何"已注册的 Avalonia lifetime/shutdown 回调"。** 转移是
`Environment.Exit` → native → **reverse P/Invoke 重入窗口过程** → 服务一次
`WM_PAINT` → render → `SyncWaitCompositorBatch`，该 compositor batch 永远不会被满足，
`WaitForMultipleObjects` 因而永久阻塞，进程**到不了 `ExitProcess`**。

因此"修正某个注册回调的行为"这一方向**不成立** —— 没有回调可供修正。

### 调用侧（已确立的顺序事实）

```
MainWindow.<.ctor>b__9_14            (MainWindow.axaml.cs:205 → Environment.Exit)
  → TopLevel.<OnOpened>b__0
    → Avalonia.Threading.Dispatcher.Send
      → TopLevel.OnOpened → WindowBase.OnOpened
        → Window.ShowCore → Window.Show
          → ClassicDesktopStyleApplicationLifetime.ShowMainWindow
            → StartCore → Start → StartWithClassicDesktopLifetime
```

gate 在 `TopLevel.OnOpened`（由 `Window.Show()` 内的 `Dispatcher.Send` **同步**引发）中运行，
即 `Environment.Exit` 发生在 **Avalonia lifetime 仍在启动途中**、`StartCore()` 尚未返回时。
这是 dump 直接给出的**顺序事实**，非假设。

### 更正：`UiStateGate` **并不**避免 `Environment.Exit`

`MainWindow.axaml.cs:185` —— `UiStateGate` 在**同一个 `Opened` handler** 中调用
`Environment.Exit(code)`，与 D3 gate（L205）结构相同。
`--uistate` / `--p2frame` / `--d3managed` / `--p2ui` 四个 gate **都是同一模式**。

其区别是 **teardown-before-termination**：它在调用 `Environment.Exit` **之前**同步执行
`CloseProject()` + 终端 `Dispose()`，而 `Environment.Exit` 只承担最终终止。
**不是**换了终止原语。

> ⚠ 早期记录曾称"`UiStateGate` 避免 `Environment.Exit`"，**该说法错误并已撤回** ——
> 当时的注释说"不依赖 `Environment.Exit`"，指的是**不依赖它做 teardown**，
> 当时把 *teardown* 与 *termination* 混为一谈。此更正**加强**了"非 D3 特有"的结论：
> D3 gate 并无结构异常，该机制是全仓共用的模式。

### 明确排除（不得再当作 D3 问题）

- **不是** `~EngineHost` / `EngineHost_Destroy` / `jthread::join` 阻塞 —— 阻塞链中
  **零** `EngineHost` / `EditorBridge` / `jthread::join` 帧。
- **不是** "runtime 线程存活 + 另一线程等它" —— 全进程仅 5 线程，阻塞者是主线程。
- **不是** finalizer 路径 —— 确有 MTA Finalizer 线程，但它不是阻塞者。
- **不是**已注册的 Avalonia lifetime/shutdown 回调 —— 栈上**没有**这种回调。
- **不是** D3 gate 缺少 `CloseProject`/`Dispose` 所致的 native teardown gap。
- **不是** 早前 NHI-1 / HRC-2 §6 的 loader/FLS 类 linger（该类为 interception/loader/FLS 形状）。

> 曾据源码提出"补 `CloseProject()`+`Dispose()` 可修复"的假设 —— 当时的依据
> （"阻塞者是 Avalonia 消息泵，不是 native teardown"）**部分错误**：
> 阻塞者确在 Avalonia 渲染/消息路径，但**并非**已注册回调，也**并非**"消息泵无人服务"，
> 而是**重入后的一次 compositor render 等待**。该修复方向因此**重新成为待验证的 A/B 实验**
> （见下），而非已知错误。

Debug 与 Release 的观测差异**成因未证明**。已确立的只是：该现象**不是配置特有的**
（见上表计数），且**阻塞路径**与**调用侧顺序**已由 dump 定位。
**未**确立"竞态"为 root cause —— dump 证明的是**阻塞发生在哪里**，
**没有**证明"该 compositor batch 为何在部分运行中永久得不到满足"。

> **仍为假设（未确立）**：D3 gate 未 `Dispose` host ⇒ GL context / Avalonia 渲染仍存活
> ⇒ `Environment.Exit` 重入窗口过程时仍有 paint/render 在飞 ⇒ 其等待的 compositor batch
> 永不完成。dump **只**确立了 render/compositor 等待与启动期顺序，
> **没有**确立"缺少 host disposal"就是该 batch 不被满足的原因。

### A/B 实验（scratch，已回滚）：teardown-before-termination **不是**充分条件

**唯一实验变量**：`Environment.Exit(code)` 之前是否同步执行
`CloseProject()` + 终端 `Dispose()`（`UiStateGate` 风格）。
runner / timeout / 判定分类 / D3 transport / ABI **全部未动**；同一 native/managed binary，
同一 gate 断言。variant B 已**回滚**并重建验证（`d3managed-B` 标记 0 处，post-revert Debug `0,0,0`）。

| 变体 | Debug（n=5） | Release（n=3） |
| --- | --- | --- |
| **A** 现状（无 teardown） | `0,0,0,0,0` → linger **0/5** | `3,3,3` → linger **3/3** |
| **B** `CloseProject`+`Dispose` | `0,0,0,3,3` → linger **2/5** | `3,3,0` → linger **2/3** |

**结论：B 变体在其测量 session 中**仍出现 process-exit linger**，
因此 B **未被证明**为可靠终止的充分条件。** B 的 `CloseProject`/`Dispose` 均正常返回，
**未**引入新的 native 失败。

> ⚠ **A 与 B 并未 interleave**（分属不同 session），因此**无法区分**
> "B 不足"与"该 session 本身处于会失败的环境状态"。
> 故此处**不**下"B 不足够"的因果结论。

> 样本量小：Debug 的 `0/5 → 2/5` 变化**不具统计意义**，但明确**不构成改善**。
> 据此，"未 Dispose host 导致 GL/渲染仍存活、compositor batch 永不满足"这一假设
> **未获支持**（见上：仍为假设）。剩余更可能是 framework/render 路径自身的终止问题。
>
> **本实验为实验性证据，不提升为 D3 实现变更**；它支持后续 framework-shutdown slice，
> **不**追溯性地使原 D3 gate 变成"错误"。**D3-Managed 维持 CLOSED（semantic/contract）。**

### A/C 配对实验（单 session interleave，单一 binary，scratch 已回滚）

**方法学修正**：此前 A/B、C 均在**不同 session** 测量，基线会漂移
（曾出现 A Release 早前 `3/3`、回滚后又 `0/3`），故跨 session 比较**无效**。
本实验改为：**单一 binary**（仅以 `--d3defer` flag 区分变体，artifact 完全相同）、
**同一 session 内 A→C→A→C interleave**、每配置 6 对。

- **A** = `Environment.Exit(code)` 直接调用（现状），在 `OnOpened`/`Window.Show()` 栈上。
- **C** = post 到 dispatcher，**下一轮 dispatcher turn** 再调用同一个 `Environment.Exit`。
  ⚠ C 的实际变化是"把终止**移出 `OnOpened`/`Window.Show()` 调用栈**"，
  **不是**"移到 `StartCore()` 返回之后"——`StartCore` 直到 shutdown 才返回。
  此为**已测试变更**的准确表述。

| 配置 | A linger | C linger |
| --- | --- | --- |
| Debug | **0 / 6** | 1 / 6 |
| Release | **4 / 6** | 1 / 6 |

**结论**：

- **Release**：A 在 interleave 下**持续复现** linger（4/6），C 显著更少（1/6）
  ⇒ **startup-order 机制获得真实因果支持**（paired 设计，基线漂移已抵消）。
- **Debug**：A 与 C 均基本干净（0/6 vs 1/6）⇒ **该配置下无可测信号**。
- **C 并未消除问题**：两个配置各仍出现 1 次 linger
  ⇒ 延后终止是**缓解手段，不是修复**。
- 历史 A `3/3` **不作为契约**；现象在本次 session 可复现（A Release 4/6）。

> 仍**未**确立：为何部分运行的 compositor batch 永不完成。
> 本实验只表明"在 startup 未完成时触发终止"是**相关因素**，
> 不等于唯一原因，也未解释 C 为何仍会 linger。

### 记录约束

Release 结果**保持**：

> `ASSERTIONS GREEN / PROCESS EXIT LINGER / verdict=FAIL`（runner exit code **3**）

**不得**改为绿色。D3 阶段"因归属与证据而 CLOSED"，与 runner "仍报 exit 3"是**两件不同的事**。
runner 的三态分类（`GREEN/GREEN`=0 · `GREEN/LINGER`=3 · `RED`=1 · artifact prereq=2）
**不因 D3 关闭而修改**。

### 证据存放

`C:\Users\14977\AppData\Local\Temp\opencode\d3-exit-linger-evidence\`
- `Release-linger.dmp`（624 MB full，linger 期间采集）
- 失败那一轮的精确 binary pair：`EditorBridge.dll`=`E8C25A0A…`（build/staged 一致）、
  `AvaloniaEditor.exe`=`B501B429…`、`AvaloniaEditor.dll`=`3AB9F57D…`
- `d3_managed_gate_Release.log`（末行 `d3managed exit=0`）

### 后续

新开 **Framework/Avalonia shutdown** slice，独立于 D3：判定正确修法是改 gate 的终止机制、
调整 Avalonia lifetime shutdown 顺序，还是别的 framework shutdown 边界。
**不得**为此改动 D3 的 transport 契约或为了让 exit code 变 0 而改 D3 gate。

---

## P2 standing：仍 BLOCKED / INADMISSIBLE

> **BLOCKED**（独立原因：P2 managed/native 契约仍 v1.2 / v1.3 不匹配）

`P2ManagedFrameGate.cs` 三处 v1.2 假设已失效，**记录待改，本次未修改**：

- L60 `expected == ((1 << 16) | 2)`（v1.2）
- L198 `EngineHostOpCount == 29`
- L199 `EngineHostOpGetPresentationFrame == 28`

当前 native 为 v1.3 / `OP_COUNT=30` / `PICK=29`，故这些断言迟早必须更新。
D3 双配置闭合后另开一个小的 **P2 Standing v1.3 Migration**。

**I2 仍为 251**（本 slice 不增加 native test inventory；257 仍只是候选值）。
### C residual dump：**未能采集**（不作任何推断）

目标：判定 C 的残余 linger 是否与 A **同一条** blocking chain。

本次 Release + --d3defer 连续 **12 次尝试全部 exit 0**，未出现 linger，
故**未获得** C residual dump。binary pair 与 A dump 相同
（EditorBridge.dll=E8C25A0A…、AvaloniaEditor.exe=B501B429…）。

**因此以下均未确立，不得推断**：

- C 是仅降低原机制触发概率，还是**存在第二种** shutdown mechanism；
- 残余 linger 的 blocking chain 是否与 A 一致；
- defer termination 与 IClassicDesktopStyleApplicationLifetime.Shutdown() 之间
  尚**无证据依据**做出选择。

> 附带观察：本次 C 为 12/12 干净，而 paired session 中 C 为 1/6 linger。
> 再次印证**每 session 计数不可当作失败概率**。
### C residual 延长观测窗口：**30/30 clean，residual 不可复现**

目标：等待**自然发生**的 C residual linger 并抓 live dump。**未改变运行语义**，
只增加观测窗口（Release + `--d3defer`，连续 30 次）：

    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0    (0/30 linger)

**这是一个有效结果**：C residual 在延长窗口内**未复现**，
因此 **residual mechanism 保持 uncharacterized**。

**据此冻结的有界结论**：

> 在已完成的 paired evidence 中，将 termination 从 `OnOpened` 调度至后续 dispatcher turn
> 观察到 Release linger 从 A 的 4/6 降至 C 的 1/6；该观察支持 **startup ordering 是
> contributing factor**。随后 C 的 12/12 与 30/30 会话均未复现 linger，
> 因此 **residual mechanism remains uncharacterized**。**C 不构成 fix。**

因此**尚不能据此比较** `defer termination` 与
`IClassicDesktopStyleApplicationLifetime.Shutdown()` 的内部机制。
**停止**继续追逐该 residual artifact；正式 framework slice 必须沿用这一限定。

> 三次 C 观测互不相同且**均不可当作概率**：paired session `1/6`、
> 首轮 capture `0/12`、延长窗口 `0/30`。
---

## Framework / Avalonia shutdown slice：OPEN（正式开启）

**正式立项**。P2 v1.3 migration **暂不启动** —— 因为 P2 standing 共享同一条退出路径，
先把 P2 接上只会把同一个未表征的 shutdown 不确定性复制进新的 standing evidence surface
（assertions GREEN → exit outcome nondeterministic → 又得 verdict=3 → 再回头解释）。

### 输入（仅限已证明的事实）

1. `Environment.Exit` 自 `TopLevel.OnOpened` 路径触发 **reverse P/Invoke** → `WndProc` →
   `WM_PAINT`/compositor → `SyncWaitCompositorBatch` → `NonPumpingSyncContext.Wait` →
   `WaitForMultipleObjectsEx`（624 MB dump 定位）。
2. A/C interleaved evidence 在 Release 观察到 `4/6 → 1/6`，支持 **startup ordering 是
   contributing factor**（单 binary、flag 区分、同 session 配对）。
3. C 随后 **12/12** 与 **30/30** 会话均未复现 linger ⇒ residual **未复现、未表征**。
4. 因此 C **不是 fix**，也**不能**用该数据选定 `Shutdown()`。
5. `CloseProject()+Dispose()` A/B **未建立充分性**（A/B 未 interleave，只能说"未被证明充分"）。

### 本 slice 的问题

> 在**不改变 D3 语义**的前提下，确定正式 termination 机制应采用
> **defer termination** 还是 **orderly Avalonia lifetime shutdown**，
> 并给出**可重复、可审计**的 acceptance evidence。

### DoD（冻结）

**必须**：

- 不修改 D3 ABI / pick transport / M1–M8 semantics；
- 不修改 runner 三态分类；
- 不用 watchdog / retry / delay / forced classification；
- **明确比较**两种正式 termination strategy；
- 至少在 **Debug 与 Release 都有可重复的 exit evidence**；
- **不得**把 `12/12`、`30/30` 写成 residual probability 或"已解决"。

**最终结果只能是两类**：

- **A** —— 选择并实现一种正式 shutdown mechanism，并证明稳定退出；
- **B** —— 证据仍不足，明确把 shutdown 留作**独立基础设施问题**，
  **不**把 P2 假装成已解除。

**只有 A 才适合随后恢复 P2 standing。**

### 流水线

```
Framework/Avalonia shutdown slice
        ↓
确定正式 termination mechanism
        ↓
fresh v1.3 native/managed pair
        ↓
P2ManagedFrameGate v1.3 migration
        ↓
restore P2 standing
        ↓
P2 GREEN only with reliable process exit
```

**D3-Managed 继续 CLOSED。P2 继续 BLOCKED / INADMISSIBLE。**
---

## `desktop.Shutdown()`：rejected as candidate（确定性 access violation）

**Finding（冻结）**：

> `desktop.Shutdown(0)` can be reached with all gate assertions GREEN, but orderly
> lifetime unwind deterministically terminates with an **access violation** in both Debug
> and Release (6/6 observed across the tested runs). The failing path enters
> `ClassicDesktopStyleApplicationLifetime.Start → StartWithClassicDesktopLifetime → Program.Main`,
> after which normal host unwinding exposes native teardown. **The exact native faulting
> operation is not yet isolated.**

| arm | Debug | Release |
| --- | --- | --- |
| Defer | `0, 0` | `0, 0` |
| Shutdown | `0x00000001` ×3，ASan `access-violation`、`==ABORTING` | `0xC0000005` ×3 |

`0xC0000005` = ACCESS_VIOLATION。断言全 GREEN 且 `desktop.Shutdown(0)` **确已到达**
（非"lifetime 未找到"的 fallback）。

**机制差异（关键）**：`Environment.Exit` **绕过** `Program.Main` 的 `using var host` 释放；
`desktop.Shutdown` 让 `Start()` 正常返回，于是 `Main` 退栈并**执行** `EditorHostService.Dispose()`
→ `EngineHost_Destroy` → `~EngineHost` → `m_RuntimeThread.join()`，此时 GL context 仍存活、
render 线程仍在运行。即"orderly"路径**暴露**了 abrupt 路径静默跳过的一处 teardown 缺陷。

> **Hypothesis**：orderly shutdown exposes a native teardown defect involving
> host/runtime destruction while rendering-related state remains live.
> **尚未证实** GL-context / render-thread race；不得写成已确认。

### 必须保留的反证线索

> `CloseProject()+Dispose()` before `Environment.Exit` **未**复现 access violation，
> 尽管 orderly `Shutdown()` 路径随后暴露了它。

因此 **"只要 Dispose 就一定崩" 不成立**。
**不得**因为 `Shutdown()` 走到了 `EngineHost_Destroy` 就直接把
`EngineHost_Destroy` 本身写成 root cause —— 真正的区别可能在**何时**、**谁的栈上**、
**GL/renderer 当时处于什么状态**。

### 原 72-run 预算作废

`Shutdown` 与 linger 不是同一类失败（确定性硬崩 vs 间歇性等待），
做次数比较无意义；已按 decision rule **C** 取消，未执行。

---

## Framework Shutdown 正式拆分为两个独立问题

### A — orderly shutdown 暴露的 native teardown access violation（**最高优先级**）

> 为什么 `desktop.Shutdown()` 让正常 host unwind 进入
> `EngineHost_Destroy → ~EngineHost → m_RuntimeThread.join()` 后产生 access violation？

调查属性佳：**确定性高**（D/R 均 3/3 硬崩）、**定位面清楚**（已到 orderly lifetime →
`Program.Main` → host unwind）、**影响范围更广**（可能影响真实正常 shutdown，不止 gate）。

第一步为 **source-first + crash artifact**：

```
desktop.Shutdown() → Start() returns → Program.Main unwind
  → EditorHostService.Dispose() → EngineHostChannel.Dispose()
  → EngineHost_Destroy → ~EngineHost → m_RuntimeThread.join()
```

需确认真正的 faulting frame 是 `~EngineHost` / `join` / 某个 GL-renderer destructor /
`EngineHost_Destroy` 内其它 teardown / 或 Avalonia-GLFW-native interop 别处。
需取 **Debug ASan crash artifact + Release crash artifact** 并比较 faulting address / native stack。

### B — abrupt `Environment.Exit` linger（**第二优先**）

> 为什么 abrupt termination 偶尔在 `OnOpened` / reverse-P/Invoke / compositor 路径中永久等待？

保持 OPEN。**暂不为此修改 D3。**特征：强 session 依赖；C 显著降低暴露；
residual 不可复现；机制已定位到 compositor wait，但**触发条件未解决**。

---

**结论**：本轮实验未找到最终修复，但把原本混在一起的一个 shutdown 问题，
清楚地拆成 **"native orderly-teardown crash"** 与 **"abrupt-exit linger"** 两条证据链。
**先查 A。**
### A slice — 第一步：source-first 结果（+ crash artifact 缺口）

**dispose 链（已由源码确认）**：

```
EditorHostService.Dispose()            (EditorHostService.cs:366)
  → EngineHostChannel.Dispose()        (EditorHostService.cs:781)
      BeginTeardown() → StopPump()
      → EngineHost_SetEventCallback(host, null, IntPtr.Zero)
      → EngineHost_Shutdown(host, ref result)
  → EngineHost_Destroy                 (capi.cpp:928；含 thread 校验)
      → ~EngineHost()                    (EngineHost.cpp:122)
          Shutdown(nullptr)
          if (m_RuntimeThread.joinable()):
              RequestEmergencyStop()        (L96-98: m_StopRequested + notify_all)
              WaitForRuntimeExit(timeout)    (L100-106: 在 m_Mutex 下等 m_LifecycleCondition / m_ThreadExited)
              JoinRuntimeIfExited()          (L108-120: m_Mutex → m_JoinMutex → join)
```

**由此确立**：源码证明 `m_RuntimeThread` 在 `~EngineHost()` **开始时是 joinable 的**，
且 teardown **显式等待其退出**（`RequestEmergencyStop` → `WaitForRuntimeExit` →
`JoinRuntimeIfExited`）。该线程承担 GL / render / tick 工作。

> ⚠ **不**据此声称该线程"在整个 teardown 期间始终存活" —— 源码只给出
> "开始时 joinable"与"显式等待退出"，**不足以**证明全过程的存续状态。
（pick service、渲染、tick 均在其中）。

**排序假设（尚未证实）**：orderly shutdown 路径下 Avalonia 在 `Start()` 返回过程中
**先**拆除 rendering / GL 资源，**之后** `Program.Main` 的 `using var host` 才执行 Dispose
⇒ runtime 线程访问已被销毁的 GL 状态 ⇒ access violation。

> 该假设与 B 实验张力**一致**：B（`CloseProject()+Dispose()` before `Environment.Exit`）
> 执行 Dispose 时 Avalonia 渲染**仍完全存活**（仍在 `OnOpened` 内，尚未拆除任何东西），
> 因此**未**复现 AV。这正是"何时 / 谁的栈上 / GL 状态如何"三者中
> **顺序**成为变量的解释。

#### Crash artifact：**未取得**（诚实记录）

- Debug ASan 仅给出 `AddressSanitizer can not provide additional info` + `==ABORTING`；
- 无 `cdb` / `windbg` / `procdump`（本机仅有 `dotnet-dump`，仅对**存活**进程有效）；
- Windows Application Error 日志中**无**对应 faulting module / offset 记录。

⇒ **faulting frame 尚未隔离**：不能确认是 `~EngineHost` / `join` / 某个 GL-renderer destructor /
`EngineHost_Destroy` 内其它 teardown / 或 Avalonia-GLFW interop 别处。
**不得**据现有证据指认 root cause。

#### 下一 cheapest 判别步骤（诊断性质，非修复）

在 Shutdown arm 中改变**唯一变量**为 Dispose 与 `desktop.Shutdown()` 的**先后**：

- B1：先 `CloseProject()+Dispose()`，再 `desktop.Shutdown()`（Dispose 时渲染仍存活）
- B2：现状 —— 先 `desktop.Shutdown()`，Dispose 随 `Main` 退栈晚发生

若 B1 不崩而 B2 崩 ⇒ **顺序假设获支持**；两者都崩 ⇒ `EngineHost_Destroy`/teardown 本身更可疑
（但仍受"Dispose 就必崩"已被 B 实验否证这一约束限制）。
### B1/B2 ordering 诊断：**ordering 不是区分因素**（H-A1 未获支持）

单一 binary pair，两 arm 仅以 flag 区分，同 session interleave，6 对/配置。

- **B1** = `CloseProject()` → terminal `Dispose()` → `desktop.Shutdown(0)`
- **B2** = `desktop.Shutdown(0)` → `Program.Main` 退栈后 `Dispose()`

| 配置 | B1（6 次） | B2（6 次） | 两者 clean |
| --- | --- | --- | --- |
| Debug | `0x00000001` ×6 | `0x00000001` ×6 | **0/6** |
| Release | `0xC0000005` ×6 | `0xC0000005` ×6 | **0/6** |

**关键验证**：B1 的日志显示 `d3-b1: CloseProject returned` → `d3-b1: Dispose returned`
→ `d3-b1: desktop.Shutdown(0)`，即 **host 在崩溃前已完整 Dispose**，随后仍以**相同**
exit code 与**相同** stderr 崩溃。

**结论**：

- **ordering 不是区分因素** —— 两种顺序都确定性崩溃，且签名完全一致；
- **H-A1 未获支持**（不再是"Dispose 与 Avalonia 渲染拆除的先后"问题）；
- **native teardown 链被排除为 faulting site**：B1 中 host 已先被完整销毁，崩溃依旧；
- 崩溃发生在 **Avalonia orderly lifetime 路径**上，且发生在 host teardown **已完成或根本不存在**之后；
  **最终的 native faulting component 尚未隔离**。
  ⚠ **不**写作"fault 位于纯 Avalonia 自身 teardown" —— 当前进程仍带有
  GLFW / GL / native rendering 环境，**尚未**证明与 EditorBridge / GL / native state 无关。

> **此前 B 实验的"张力"到此解决**：B（`CloseProject()+Dispose()` 后接
> `Environment.Exit`）之所以**不崩**，是因为 `Environment.Exit` **根本不进入**
> Avalonia 的 orderly teardown。真正的区分变量从来**不是** Dispose 顺序，
> 而是**走哪条终止路径**。

> **仍为假设**：崩溃的具体 native frame **未隔离**（无 cdb/windbg，ASan 无额外信息，
> 无 Application Error 记录）。可能位于 Avalonia 的 Win32/GLFW/renderer 拆除，
> 亦可能与 native host 创建/销毁的 GL 状态交互有关。**不得**指认 root cause。
### MR-A：Avalonia-only 最小复现 —— **同样 AV**（强支持 Avalonia/Win32 lifetime 自身）

**构建位置在仓库之外**（`%TEMP%\opencode\mr-avalonia-only`），**未修改任何现有工程**。

MR-A 只包含：`Program.Main` → `StartWithClassicDesktopLifetime` → 一个 `Window` →
`Opened` → `desktop.Shutdown(0)`。**不包含**：`EditorHostService`、`EngineHostChannel`、
`EngineHost`、GLFW、我们自己的 OpenGL context、`EditorBridge.dll`、runtime thread、D3 gate。

已核验 repro 输出目录中 **`EditorBridge*` 文件数 = 0**（Debug 与 Release 均是）。

| 配置 | n | 结果 | clean |
| --- | --- | --- | --- |
| Debug | 3 | `0xC0000005` ×3 | **0/3** |
| Release | 3 | `0xC0000005` ×3 | **0/3** |

两者均确认到达 `mr: Opened -> requesting desktop.Shutdown(0)`。

**结论**：access violation 在**纯 Avalonia**（11.2.7 + FluentTheme + 单 Window）下即可复现，
与 `EngineHost` / `EditorBridge` / 我们的 GL 代码**无关**。
⇒ **强支持问题位于 Avalonia / Win32 lifetime 自身**（或其对 OS/图形栈的使用），
**不是** host-specific native-state 交互。

> **附带解释一个此前的不一致**：主工程 Debug 报 `0x00000001`（ASan abort）而 Release 报
> `0xC0000005`；MR-A 两配置**均为** `0xC0000005`（无 ASan 插桩）。
> ⇒ 二者是**同一底层 AV**，差异只来自 ASan 是否插桩，**不是**两种不同故障。

> ⚠ MR-A 仍是 Avalonia 的 **Win32 backend**，其下仍会使用原生 Win32 / 图形 API。
> 因此表述为"Avalonia/Win32 lifetime 路径"；**尚未**定位到具体 native frame
> （无 cdb/windbg）。**不**等同于"已定位 Avalonia 内部某行代码"。

> **影响**：`Shutdown()` 在此 Avalonia 版本上**不可用**作为本应用的终止路径；
> 这也意味着该 AV **不是** D3 引入，且**不限于** gate 场景 —— 任何使用
> `desktop.Shutdown()` 的正常应用退出都会命中。
> 值得作为**上游 Avalonia 缺陷**评估/上报。

**未执行 MR-B**（GL/native rendering 分支）：MR-A 已 AV，无需再加 GL 来解释。
### MR-A 变体测试（Post / Window.Close）—— **三个变体均无法退出**

保持 MR-A 最小：无 EditorBridge、无 EngineHost、无 OpenGL、无 D3 gate、无 Dispose、无 defer termination。

| 配置 | v0（同步 Shutdown） | v1（Post → Shutdown） | v2（Post → Window.Close） | clean |
| --- | --- | --- | --- | --- |
| Debug | LINGER ×3 | LINGER ×3 | LINGER ×3 | **0/3** |
| Release | LINGER ×3 | LINGER ×3 | LINGER ×3 | **0/3** |

随后对 v0（Release）加测 n=8：`LINGER ×8`，clean 0/8、AV 0/8。

**结论**：

- **v1 不改变结果** ⇒ "同步在 `Opened` 内调用"**不是**触发条件（延后一轮无效）；
- **v2 不改变结果** ⇒ **不是** `Shutdown()` API 路径专属；窗口关闭路径同样无法退出；
- 三者当前**行为一致**，均无法退出。

> ⚠ **必须同时记录的观测不稳定性**：同一 v0 路径在**约十分钟前**为 `0xC0000005` ×6
>（Debug 3/3 + Release 3/3），改为 hang 8/8，**期间 binary 已重建**、repo 未改。
> 因此 **MR-A 的 AV 并非稳定可复现**；上文记录的 "3/3 AV" 应视为**一次观测窗口**的结果，
> **不是**确定性结论。orderly shutdown 失败会**表现为 AV 或永久挂起两种形态**。

> 由于三变体当前不可区分，本轮**未**排除任何候选解释；
> "Post 可规避" 与 "Close 可规避" **均未成立**。
> 若要把 AV 与 hang 作为两条独立机制区分，仍需 native 调试器或更多重复观测。
### Framework A 外部 evidence receipt：upstream repro 已打包

**位置**：`%TEMP%\opencode\avalonia-upstream-repro\`（**仓库之外**，主 repo 未改）

```
ISSUE.md                       issue 正文（含 Limitations）
repro/MrAvalonia.csproj        Avalonia 11.2.7 + Desktop + FluentTheme
repro/Program.cs               单 Window + Opened，v0/v1/v2 三条终止路径
repro/app.manifest             DPI-aware
```

**已核验**：打包副本可 clean build，且文档所述失败**在打包副本上复现**
（v0 n=2：`LINGER, LINGER`，均非 clean）。

**标题（避免把根因写死为 Win32 bug）**：

> Win32 desktop lifetime shutdown fails to terminate in minimal Avalonia repro
> (`Shutdown` / `Window.Close`)

**报告口径**：明确写为 *"fails to terminate"*，而非"deterministic crash"；
failure mode 记为 **AV (`0xC0000005`) 或永久 hang**，跨 session 不稳定；
明确声明 **native faulting operation 未隔离**（无 cdb/windbg，ASan 无额外信息，无 WER 记录）。

**排除项已在报告中列出**：无 EditorBridge / EngineHost / GLFW / 自有 OpenGL context /
runtime thread / D3 gate；`DllImport`/`NativeLibrary` 计数为 0，输出目录无自有 `.dll`。

**须一并声明的 caveat**：宿主 OS 为 **Windows 11 Insider Preview build 29671**，
无法排除 OS 特异性贡献；未在 retail Windows 上验证。

> **结论边界**：MR-A 证明了 orderly lifetime failure **不依赖** EngineHost/EditorBridge，
> 但**没有**证明具体 fault 属于 Avalonia 的哪一层 native implementation。
> 故标题与正文均避免指认层级。

**尚未提交**：issue 文本已就绪，等待人工审阅/提交。
### Sequencing 调整：P2 v1.3 migration 可与 upstream 调查并行

Framework A 现已证明为 **外部问题 + 无本地 workaround + 有独立 upstream repro**，
继续把 P2 migration 完全冻结会把一个**当前 repo 无法解决**的问题扩散成项目内部停工。

```
Framework A                              OPEN / upstream-blocked (MR-A verified)
        ↘  （并行）
P2 v1.3 contract migration              可以开始
P2 standing                              仍 BLOCKED / INADMISSIBLE
        ↓
Framework A 得到可接受解决/规避证据
        ↓
恢复 P2 standing
```

**允许**：P2 v1.3 **contract migration**（已知、有限、机械性的 ABI/managed 对齐）。
**不允许**：恢复 P2 standing、给出 P2 GREEN。standing 仍受 shutdown evidence substrate 阻塞。

### Upstream review 结果（**未提交**）

三项检查通过：标题未隐含 root cause；正文分开 observed / excluded / unknown / questions；
环境 caveat 写成 limitation 而非 diagnosis。保留 Insider Preview build 29671 caveat。

Review 中发现并修补的**两个真实缺陷**：

1. **render mode 未固定也未记录** —— 使用默认 `UsePlatformDetect()`，但从未记录实际选中的是
   software / D3D-ANGLE / WinUI 哪种。对一个可能涉及 compositor 或 render thread 的
   lifetime 失败，这是关键变量。已在 issue 中**明确标注为 unknown**，并表示可按上游
   要求用 `UseRenderingMode(...)` 固定。（尝试用 API 打印时引入了 4 个编译错误，
   **已回退**，repro 恢复 clean build 且失败仍复现。）
2. **"no runtime thread" 表述不精确** —— 可能被误读为"无任何线程"。
   已改为"无**应用级** runtime thread"，并明确 Avalonia 自身的 dispatcher /
   compositor 线程仍然存在；排除项移除的是**我方** native stack，不是 Avalonia 内部。

**状态**：issue 文本就绪，**未提交**。发布需显式授权。
---

## P2 v1.3 contract migration：完成（**standing 仍 BLOCKED**）

**边界**：本 slice 只让 P2 gate 能正确理解当前 **v1.3 pair**；
**不**以让 P2 standing 变 GREEN 为目标。

### 更正：只有**两条**断言失效，不是三条

先前记录的"三处 stale assertions"经逐条核对后修正为 **两条失效断言 + 一条过期提示文本**：

| 行 | 断言 | v1.3 下 | 处理 |
| --- | --- | --- | --- |
| M1 L60 | `expected == ((1<<16)\\|2)` | **失效** | 改为 v1.3 |
| M6/M7 L198 | `OpCount == 29` | **失效** | 改为 30 |
| L196 提示文本 | `"...EngineHost v1.2"` | 过期文本 | 改为 v1.3 |
| L195 | `ExpectedVersion == v1.1` | **仍成立** | 不动（独立版本线） |
| L197 | `GetPresentationFrame == 28` | **仍成立** | **不动**（PICK 占用 29，P2 未重编号） |
| L199 | `NoNewFrame == 11` | **仍成立** | 不动 |

> **不得**把 `op == 28` 一并"迁移"：D3 v1.3 未给 P2 重新编号。

### Artifact freshness（复用 D3 M8-A 纪律，不以"0 errors"为充分证据）

| 配置 | build SHA256 | staged SHA256 | identity | staged 新于 native source |
| --- | --- | --- | --- | --- |
| Debug | `504FABBF06E67DFD…` | `504FABBF06E67DFD…` | ✅ | ✅ |
| Release | `E8C25A0A696C567D8FF2…` | `E8C25A0A696C567D8FF2…` | ✅ | ✅ |

Debug/Release build 0 errors（**必要非充分**）；8 warnings 为既有
（`AvaloniaEditor.cs` / `MainViewModel.cs`），本次 migration **未新增** warning。

### P2 断言结果（与 process-exit 分开记录）

```
[P2-M1] version lockstep OK (native=managed=0x00010003 = v1.3)   ← migration 后生效
[P2-M2] packet offsets/size + rejection paths OK (header 16B, total 1,920,016B)
[P2-M4] NO_NEW_FRAME never reuses an old frame
[P2-M3/M5] 3 consecutive frames decoded; last frame_id=3; atomicity + monotonicity OK
[P2-M6/M7] inventory: opGetPresentationFrame=28 (opCount=30), statusNoNewFrame=11
[P2-MANAGED GATE] ALL GREEN
[p2-gates] p2frame GREEN exit=0 32s
[p2-gates] p2ui    GREEN exit=0 46s
```

- **断言层**：P2 在 **v1.3 pair** 下 M1–M7 ALL GREEN，contract 迁移目标达成。
- **process-exit 层**：`p2frame` 与 `p2ui` 本次**均正常退出**（exit=0）。

### 但 standing **仍然 BLOCKED / INADMISSIBLE**

runner 本次自报 `ALL GREEN`，**但不得据此解锁 standing**：

1. Framework A（共享 shutdown substrate）仍 **OPEN / upstream-blocked**；
2. abrupt-path linger 的 residual **仍未表征**且已知**强 session 依赖**
   （同 binary 曾出现 A Release `3/3` → `0/3` → `4/6`）；
3. 本次"两场景均正常退出"**只是一次观测窗口**，不构成 substrate 可接受的证据。

> 即便断言全绿且进程正常退出，只要共享终止路径仍可能被判定为不可靠，
> P2 GREEN 就不能作为可信证据面。**migration 完成 ≠ standing 解除。**

**未修改**：`run_p2_gates.ps1`（0 diffs）、runner 三态、D3 ABI/transport、M1–M8 semantics、
P2 frame semantics；**未**把 Framework A 的 shutdown workaround 带入 P2 gate。
---

## Repository-hygiene slice：OPEN（审计阶段）

### Blocker：case-colliding tracked paths

> `engine/src/Core/Application.cpp` 与 `engine/src/core/Application.cpp` 在当前
> **大小写不敏感的 Windows 工作树**中无法被独立表示。

**D3/P2 landing 中不允许**任何 deletion / merge / overwrite / index surgery。

### 证据（case-sensitive checkout，WSL ext4，已验证 case-sensitive）

| 项 | 值 |
| --- | --- |
| clean checkout HEAD | `d3ba590`（branch `avalonia`） |
| 两个 path 在 Linux 上物理独立存在 | ✅（`Core/Application.cpp`、`core/Application.cpp`） |
| `Core/Application.cpp` blob | `b9c39f4f6c4df613c395e038c0bfcc9ccb7900ac` |
| `core/Application.cpp` blob | `34d517ec7fa9c789217023c20767e6bc9253b84b` |
| 差异 | **仅 +9 行**（`9 insertions, 0 deletions`，`{Core => core}` 形式） |

### 那 9 行的内容：历史 shutdown 诊断 instrumentation

```cpp
// GP1-DX 启动调查（2026-08-26）：主循环退出点取证。若此前有 "WindowClose: OS-initiated" 告警，
// 则 ShouldClose 由内部置位（需排查 glfwSetWindowShouldClose 调用者）；有则来自 OS 关闭消息。
{
  const double uptimeSec = Time::GetElapsedSinceInit();
  Log::Warn("Main loop exited after {:.1f}s (see preceding "
            "WindowClose warning for origin)", uptimeSec);
}
```

### 判定（对应三条判据）

| 判据 | 适用 | 结论 |
| --- | --- | --- |
| 9 行有独立语义 | **否** | — |
| 9 行属重复 / 历史残留 | **是** | 构成 hygiene fix 的**可审计候选**；**仍不得**在本 landing 中删除 |
| 涉及当前 D3/P1 逻辑 | **否** | **不因源码完整性暂停 landing**（暂停原因见下） |

### Compile-graph 结果：**strong provisional**

> 由 POSIX 大小写敏感 glob 求值、使用 `engine/CMakeLists.txt` 的**确切** glob 列表得出；
> **尚未由 CMake 自身执行验证**（WSL 无 `cmake`，安装失败：无 sudo / 无网络）。

结果：glob 仅命中 `src/Core/Application.cpp`；无任何 glob 枚举 `src/core/`。
因此 lowercase 文件**不进入编译图**，其额外内容**不构成 engine/D3/P1 运行语义**，
在 Linux 上整体属于**未编译的死代码**。

### Root cause：谨慎表述

> **likely introduced or preserved through a case-insensitive filesystem workflow**

**不**将其写为已确认的历史事实 —— 目前**没有**历史 commit 证据。
已确定的只是**结果**与**环境冲突**。

### 候选修复（**candidate fix，非已批准修改**）

> 删除 `engine/src/core/Application.cpp`（lowercase entry）。

依据：(a) 不进入编译图；(b) 唯一独特内容为不执行的诊断 instrumentation。

### Hygiene slice DoD（正式）

1. 在 case-sensitive checkout 上确认删除后仅剩 `Core/Application.cpp`；
2. 用**真实 CMake/build** 验证编译图（补上 provisional 证据）；
3. 确认没有其他文件引用 `src/core/Application.cpp`；
4. Debug/Release build 通过；
5. Windows 工作树不再有 case-collision；
6. 两个 blob 的语义损失明确为**仅删除废弃诊断 instrumentation**。

### 与 Framework A/B 的关系（明确不混淆）

该 instrumentation 属**shutdown exit forensics**（"main loop exited"、"WindowClose: OS-initiated"、
`glfwSetWindowShouldClose` 调用者排查），与 Framework A/B 属**同类问题**。

> 这说明**shutdown-exit forensics 在本仓此前已被尝试过并留下未清理残留**；
> 但它**不构成**关于当前 AV / linger 的任何证据，不作此推断。

### 对 D3/P2 landing 的影响：继续暂停

当前 Windows checkout **无法同时表达**：

- `Core/Application.cpp` 的当前修改版；
- `core/Application.cpp` 的原始 635-line blob。

在 hygiene 修复之前，任何 landing 都必须依赖**已被明确禁止**的 index surgery。

> 补充观察：因只有 `Core/Application.cpp` 进入编译图，正确 landing 应把 D3/P1 变更写入 `Core/`。
> 这使本次 landing 在 Windows 上**不只是别扭，而是与 case-sensitive checkout 的源码拓扑不兼容**。

### 本阶段遵守的边界

- **未**触碰两个 `Application.cpp`；
- **未**执行 `git add` / `git rm --cached` / case-normalization；
- **未** staging、**未** commit、**未** push；
- HEAD 仍为 `d3ba590`，staged = 0。
---

## 三个独立 slice 的重新分层（F1 审计续）

### S1 — ScriptInstance default-argument defect（**已完成，独立 commit**）

**范围**：仅 `engine/include/Engine/Scripting/ScriptInstance.h`。

**根因（不是 GCC 13 缺陷 —— 已被 clang 诊断否证）**：

`ScriptInstance::Config` 是**嵌套类型**且带 **NSDMI**（默认成员初始化器）。
按 [class.mem]，NSDMI 在其所属类完整之前**不在作用域内**，因此外层类的成员函数
声明中 `= {}` **无法读取**这些初始化器。clang 诊断原文：

> default member initializer for 'b' needed within definition of
> enclosing class 'Outer' outside of member functions

GCC 13 同样拒绝该形态。隔离矩阵：

| 形态 | 结果 |
| --- | --- |
| 非嵌套 + NSDMI + `= {}` | PASS |
| 嵌套 + **无** NSDMI + `= {}` | PASS |
| 函数内局部 struct + NSDMI + `= {}` | PASS |
| 类外限定名 + NSDMI | PASS |
| **嵌套 + NSDMI + `= {}`（本例）** | **FAIL（GCC 13 与 Clang 18 一致）** |

**修复**：移除两参签名上的 `= {}`，新增单参重载转发 `Config{}`。
两参签名 `const Config&` **保持不变**，两种调用形态不变。

**验证（CI-equivalent Ubuntu 24.04 / GCC 13.3 容器）**：

- `ScriptInstance.h:48` 错误数 **3 → 0**；
- `bridge/src/capi.cpp` 与 `bridge/src/EditorSession.cpp` **成功编译**
  （`capi.cpp.o`、`EditorSession.cpp.o` 均产出）；
- `-std=gnu++17` 与 `gnu++20` 下 `Initialize(path)` 与
  `Initialize(path, Config{...})` 两种调用均通过；
- 剩余失败与 pristine baseline **完全一致**（见 S2），证明 S1 **未掩盖**任何其他问题。

**commit**：`b15b1c1`，`1 file changed, 33 insertions(+), 10 deletions(-)`，
**仅含 S1 单个文件**，未混入 F1 或 portability 变更。

> 附带发现：CI 的依赖清单**缺少 `libvulkan-dev`**，而 GLFW 的 `glfw3.h` 需要
> `vulkan/vulkan.h`。这是 CI 环境缺口，与 S1 无关。

### F1 — Application.cpp case-collision（**仍 OPEN**）

| DoD | 状态 |
| --- | --- |
| 1 仅剩 `Core/Application.cpp` | ✅ CLOSED |
| 2 真实 CMake configure（Debug+Release） | ✅ CLOSED，`compile_commands.json` 中 lowercase 引用 = 0 |
| 3 无 source/build/IDE 引用 | ✅ CLOSED |
| 4 Debug build PASS | ❌ **BLOCKED** |
| 5 Release build PASS | ❌ **BLOCKED** |
| 6 删除内容仅为废弃 GP1-DX diagnostics | ✅ CLOSED |

> **DoD 4/5 BLOCKED**：*pre-existing Linux build portability defect in
> `RHIWindow.cpp` (`glfw3native.h` / Windows-native GLFW dependency).
> This blocker reproduces on pristine `d3ba590` and is independent of the F1 deletion.*

**明确不写成** "F1 build failed" —— 已证明失败原因**不是** F1。

**F1 不 CLOSED**，且按既定 DoD（1–6 全 CLOSED）**不得**现在提交，
以免产生半闭环历史。

### S2 — Linux RHI portability（**待独立审计**）

`engine/src/RHI/RHIWindow.cpp:21` 无条件 `#include <GLFW/glfw3native.h>`，
该头文件在第 105 行 include `windows.h` ⇒ **engine 在 Linux 上无法编译**。

**先审计再决定**：需要查清

1. 它**为什么**需要 `glfw3native.h`；
2. 实际使用了**哪些符号**；
3. 非 Windows 上是否应存在**替代实现**。

**不得**仅为清除 F1 的 DoD 而直接加 `#ifdef _WIN32`。

### 顺序

```
S1 ScriptInstance              ✅ 独立 commit (b15b1c1)
F1 Application.cpp             OPEN，等共享 Linux build substrate
S2 RHIWindow Linux portability  待独立审计 → 最小修复
S1 + S2 完成后 → 重验 F1 DoD 4/5 → F1 CLOSED → F2 landing
```

> 关键边界：**F1 不承担"让 engine 第一次在 Linux 上 build"** 这一额外职责。
> 否则一个 repository-hygiene deletion 会逐渐变成 Linux portability 清理集合，
> 最终无法证明改了什么、为什么改。
---

## S2 — Vulkan / RHI portability（OPEN，**未修**）

**状态**：`OPEN`。经 S2-A + S2-B 两轮**只读**审计，
已证明其**远大于"加一个 include guard"**。本节不含任何代码修改。

### S2-A — Linux 编译可移植性

```
engine/src/RHI/RHIWindow.cpp:21-23
  #define GLFW_EXPOSE_NATIVE_WIN32
  #include <GLFW/glfw3native.h>   → 该头文件在第 105 行 include windows.h
```

`RHIWindow.cpp` 由 `engine/CMakeLists.txt:30` 的 `src/RHI/*.cpp` **无条件** glob 编译，
因此 engine 在 Linux 上无法编译。

**该 include 的唯一用途**：L57 `m_HWND = glfwGetWin32Window(m_Window);`（1 处）。

| 符号 | 使用次数 | 来源 |
| --- | --- | --- |
| `glfwGetWin32Window` | 1 | 仅 `glfw3native.h` |
| `glfwCreateWindowSurface` | 2 | **`glfw3.h`**（L17 已 `GLFW_INCLUDE_VULKAN`） |
| `glfwGetX11Window` / `glfwGetWaylandWindow` | 0 | 未使用 |

> 即：Vulkan surface **不是**该 include 的理由；真正依赖仅 `glfwGetWin32Window`。

### S2-B — Vulkan WSI 抽象错配（**实质缺陷**）

完整调用链（全部为 tracked 代码）：

```
RHIDemoApp.cpp:91   InitVulkan(), #if ENGINE_HAS_VULKAN（**不受 WIN32 限制**）
  └─ dev->Initialize(m_RHIWindow->GetHWND(), w, h)
       ├─ VulkanDevice.cpp:118  if (windowHandle) → instanceExts += "VK_KHR_win32_surface"
       ├─ VulkanDevice.cpp:181  if (windowHandle) → VkWin32SurfaceCreateInfoKHR
       └─ VulkanDevice.cpp:183  GetModuleHandle(nullptr) / (HWND)windowHandle
```

**`VkWin32SurfaceCreateInfoKHR` 与 `GetModuleHandle` 是硬编码 Win32 调用**，
而 `ENGINE_HAS_VULKAN` 由 `CMakeLists.txt:131-135` 的 `find_package(Vulkan QUIET)`
在**任意平台**启用。

**因此这不是"Win32 句柄泄漏进可移植抽象"，而是整条 Vulkan WSI 路径为 Win32-only。**
`GetHWND()` → `m_HWND` 只是入口；Win32 假设在其下游已被固化，
**与 `GetHWND()` 返回什么无关**。

### 审计问答结论

1. **`GetHWND()` 是否应保持 Windows-only？** 是。头文件已注明 `/** 获取 Win32 窗口句柄 */`，
   真实消费者是 D3D12 路径；`RHIDemoApp.cpp:91` 的 Vulkan 使用应视为**误用**。
2. **Vulkan surface 是否应走 `CreateVulkanSurface()`？** **应**——而该函数
   **零调用者**（声明 `RHIWindow.h:58`、定义 `RHIWindow.cpp:90`、
   经 `glfwCreateWindowSurface` 实现、平台中立、**完全未被使用**），
   实际路径却在 `VulkanDevice` 中手写 Win32 surface。**正确抽象已存在但被绕过。**
3. **`windowHandle` 是否为 D3D12 遗留？** 实质上是。`void* windowHandle`
   把 Win32-only 语义带入 Vulkan API；无头工厂 `VulkanDevice.cpp:383` 传 `nullptr` 以完全退出。
   它实际是"Win32 HWND 或 null"的标志位，而非通用窗口句柄。
4. **Vulkan WSI 是否意图支持 X11/Wayland？** **无任何证据**。全树不存在
   `VK_KHR_xlib_surface` / `xcb_surface` / `wayland_surface`；
   `VulkanSwapChain.cpp` **完全不含** surface / extension 引用。

### 三个独立缺陷（不是一个）

| # | 缺陷 | 性质 |
| --- | --- | --- |
| (i) | `glfw3native.h` 无条件 include | 编译期，Linux 直接失败 |
| (ii) | `VulkanDevice` 硬编码 `VK_KHR_win32_surface` / `VkWin32SurfaceCreateInfoKHR` / `GetModuleHandle`，而 `ENGINE_HAS_VULKAN` 不受平台限制 | **运行时，Vulkan 后端在 Linux 上本就无法工作** |
| (iii) | 平台中立的 `CreateVulkanSurface()` 为死代码 | 架构 smell |

**结论**：给 include 加 `_WIN32` 只会把**编译错误转成运行期失败**，因此**不是候选修复**。

### 明确未决的产品决策（**不在本 slice 内**）

是否将 Vulkan 以 `WIN32` 门控（使 Linux 上 Vulkan 不可用而非半可用）。

证据只支持：当前实现是 Win32-only、Linux Vulkan 支持不完整、平台中立路径已存在。
这**不等于**"Vulkan 本就只打算支持 Windows"。
改门控会把**偶然限制变成显式产品决策** —— 属 backend-support slice，
**不得**作为 build unblock 的副产品。**本次不做。**

---

## F12 — CI dependency declaration incomplete（OPEN，独立于 S2）

> `cmake-multi-platform.yml` 声明的 Linux 依赖**不足以复现构建环境**：
> 缺少 `libvulkan-dev`，而 GLFW 的 `glfw3.h` 需要 `vulkan/vulkan.h`。

- **证据**：容器内构建必须额外安装 `libvulkan-dev` 才能越过该错误；
- **影响**：本地 / 容器"CI-equivalent"环境需要**额外包**才能复现 CI，
  因此 S1 与 S2 所依赖的"CI 等价"前提本身是**近似的**；
- **范围**：**workflow / 验证基础设施**卫生，**不**属于 RHI 可移植性，**不**并入 S2。

---

## 当前状态（分层，保持可归因）

```
S1  ScriptInstance compatibility   CLOSED  (b15b1c1, 单文件)
F1  Application.cpp case collision FIX READY
        DoD 1/2/3/6 complete | DoD 4/5 blocked by S2
S2  Vulkan/RHI portability          OPEN    S2-A + S2-B, 未修
F12 CI dependency declaration      OPEN    独立 finding
```

**F1 边界仍然成立**：F1 是 repository-integrity 缺陷（大小写冲突 tracked path、
dead lowercase artifact、无 build 引用、无语义损失、deletion-only）。
DoD 4/5 的失败**已不再**是"alias 可能破坏构建"，而是：

> 仓库在 `d3ba590` **无法产出完整 Linux engine 构建**，因为存在**另一个**
> pre-existing 可移植性缺陷。

这属于**不同缺陷类别**；若继续挂在 F1 下，会使 F1 历史产生误导。

**F1 未 CLOSED**（DoD 4/5 未满足），**未提交**。
---

## 证据来源更正：S2 全部结论均基于 **HEAD `d3ba590`**（非工作树）

**性质**：文档卫生更正，**不**产生新的技术发现；**S2-A/S2-B 结论不变**。

**为何需要更正**：S2 审计期间，工作树中 `RHIWindow.cpp` 存在**与 S2 无关的
pre-existing 修改**（`#include "Engine/Platform/GlfwLifetime.h"`、
`#include <exception>`、`m_OwnerThreadId(std::this_thread::get_id())`）。
混用来源会污染可追溯性，故在此显式分离。

### 证据基础：HEAD `d3ba590`

以下 S2 结论**全部**可在 HEAD 中直接验证：

- **S2-A** —— HEAD `engine/src/RHI/RHIWindow.cpp`：
  - `L15  #define GLFW_INCLUDE_VULKAN`
  - `L19  #define GLFW_EXPOSE_NATIVE_WIN32`
  - `L20  #include <GLFW/glfw3native.h>`
  - `L50  m_HWND = glfwGetWin32Window(m_Window);`
  ⇒ 无条件的 Win32 native GLFW 暴露与 include 路径**存在于 HEAD**。
- `RHIWindow::GetHWND()` 调用链**存在于 HEAD**。
- `RHIDemoApp.cpp` 的 Vulkan / D3D12 调用点**存在于 HEAD**。
- `VulkanDevice.cpp` 的 Win32 Vulkan WSI 构造**存在于 HEAD**。
- `ENGINE_HAS_VULKAN` 的平台门控行为**存在于 HEAD**。

### 证据基础：工作树（dirty，126 entries）

> **除被审计对象本身外，不用于任何 S2 结论。**

- `RHIWindow.cpp` 存在与 S2 无关的既有修改；
  该文件**不是**作为 S2 的一部分被撰写的；
- 已核验 S2 所依赖的 3 行（`GLFW_EXPOSE_NATIVE_WIN32` / `glfw3native.h` /
  `glfwGetWin32Window`）在 HEAD 与工作树中**逐字相同**
  （HEAD L19/L20/L50 ↔ WT L22/L23/L57，差异仅为行号偏移）；
  因此**结论不受来源选择影响**。

### 措辞更正

**已作废的措辞（保留仅作对照，勿引为结论）**：
> RHIWindow.cpp:21-23 hardcodes GLFW_EXPOSE_NATIVE_WIN32...

**更正后**：
> In HEAD d3ba590, RHIWindow.cpp contains an unconditional Win32 native GLFW
> exposure/include path (`GLFW_EXPOSE_NATIVE_WIN32` + `<GLFW/glfw3native.h>`),
> whose only use is `m_HWND = glfwGetWin32Window(m_Window);`.

并附注：

> The S2-A/S2-B trace was verified against committed sources. The working tree
> contained unrelated pre-existing modifications in RHIWindow.cpp; those were
> excluded from the analysis except where confirmed identical to HEAD context.

---

## 三重现实（任何 landing 之前必须保持分离）

当前同时存在**三种**状态，此前记录未充分区分：

### ① HEAD `d3ba590` —— 仓库基线

- 322 commits 的 branch `avalonia` 尖端；
- 本记录中所有 S1 / S2 / F1 证据结论的**唯一权威来源**；
- **未**包含 S1 修复，**未**包含 F1 deletion。

### ② 工作树（dirty）—— 126 entries

- 其中**绝大多数为本次会话之前既有**的未提交工作（HRC-0→HRC-3、P1、P2、D3 等）；
- 含 `RHIWindow.cpp` 等与 S2 无关的既有修改；
- 含本会话撰写但**未提交**的 `ScriptInstance.h`（S1 修复）；
- **本会话未**新增对 `RHIWindow.cpp` / `VulkanDevice.cpp` / `RHIDemoApp.cpp` 的修改。

### ③ 隔离的 slice 产物（不在 HEAD，也不在工作树）

| Slice | 位置 | 状态 |
| --- | --- | --- |
| S1 | scratch clone `b15b1c1` + 主树 unstaged 编辑 | 验证完成，**未进入 HEAD 历史** |
| F1 deletion | scratch clone staged（**未 commit**） | deletion-only，DoD 1/2/3/6 成立 |

> **重要后果**：S1 与 F1 **都**受同一个障碍约束 —— 在 126-entry dirty 工作树中，
> `Application.cpp` 大小写冲突使**目录级** `git add` 无法完成，
> 因此 `ScriptInstance.h` 也无法先于 F1 进入真实历史。
> **F1 是 S1 抵达真实历史的共同前置。**

---

## 当前状态（三层分离）

```
HEAD d3ba590                 仓库基线；S1/F1 均不在其中
  └─ 工作树 126 entries        多为既有；含 unstaged S1；含既有 RHIWindow 修改
       └─ 隔离产物              S1 b15b1c1 (scratch) | F1 deletion (staged, uncommitted)

S1  ScriptInstance compatibility   CLOSED (b15b1c1) —— 未进入 HEAD 历史
F1  Application.cpp case collision FIX READY —— DoD 1/2/3/6 成立, 4/5 blocked
S2  Vulkan/RHI portability          OPEN —— audit finding, 未授权修改
F12 CI dependency declaration      OPEN —— 独立 finding
```

**本次更正为文档卫生，不推进任何 S2 修复。**
下一个决策点不变：S2 是审计发现而非已授权变更；F1 因 DoD 4/5 被独立 Linux
可移植性问题阻塞而保持 OPEN；S1 已验证但不在主历史中。
---

## S2 Provenance — **Outcome C: intent unresolved**

仓库中关于 Vulkan 平台范围存在**相互冲突的信号**。

`docs/D3D12-Integration-Analysis.md` 描述了 **Vulkan-first 跨平台**策略，
并把 D3D12 定位为仅 Windows 的附加后端。但**已提交的源码历史中，
找不到与之对应的四平台 Vulkan 实现阶段、roadmap 条目、closure 记录或可移植性工作**。

历史上的可移植性工作主要在**阻止 D3D12 专属依赖影响 Linux 构建**
（`c9edaf5`/`00cc497` "CMake cross-platform D3D12 protection"、
`30d7761` "guard d3d12ma link behind TARGET check for Linux compat"、
`13b7fc2` "cross-platform compile errors"、`d9a5992` `fopen_s`→`fopen`），
**并未演进 Vulkan 的可移植性**。RHI 层引入的 Vulkan 文件此后极少迭代
（`VulkanLoader.cpp` 2 commits、`RHIWindow.cpp` 1 commit），
而当前 HEAD 在**由通用 Vulkan 检测启用的路径**中仍含 Win32 专属的
loader / native-window / surface-creation 假设。

**因此**：

- 当前实现表现为 **Windows-only 的 Vulkan 路径**；
- 已文档化的 Vulkan-first 策略**可能**代表意图方向，但
  **其批准状态与交付承诺无法从仓库证据确立**；
- 将此状态转化为"把 Vulkan 门控到 Windows"或"恢复跨平台 Vulkan"，
  属于**产品决策**，而**非**机械的可移植性修复。

> **措辞边界**：记录为 **"no evidence found of a committed Vulkan portability target"**，
> 而**非**断言"Vulkan 从来不是既定目标" —— 后者属于把推断写成历史事实。

**Disposition**：**freeze S2 at audit**。任何变更都需要显式的 backend strategy 决策。

### 附带发现：Linux CI 实际上处于无监控状态

`cmake-multi-platform.yml:47-59` **确实**包含 `ubuntu-latest`（`g++` 与 `clang++`）。
但 pristine `d3ba590` 在该环境**无法构建成功**（已实证），
因此 Linux matrix 长期处于红/被忽略而未被视为信号。
叠加 **F12**（缺 `libvulkan-dev`），可判定 **Linux CI 腿实际未被有效监控**。
---

## 引擎能力审计（新增独立文档）

完整子系统成熟度矩阵、与主流引擎（Unreal 5 / Unity / Godot 4 / Bevy）对比、
逐子系统的验证状态矩阵，以及按差距点给出的演进规划（P0–P3）见：

> **`docs/Engine-Capability-Audit.md`**

**审计基准**：HEAD `d3ba590`。**HEAD-scoped** —— 工作树有 126 项未提交条目，
领先于 HEAD，故本审计可能低估实际进度。

**风险加权最高的缺口**（非体量最大，而是失败静默性最强）：

| 子系统 | 体量 | 测试 |
| --- | --- | --- |
| **Animation** | 29 headers | **0** |
| **Audio** | 13 文件 | **0** |

二者均声称完整却零测试；未测试的复杂代码只会静默失败。
其次是**完全缺失**的 NavMesh 与 Networking（与主流引擎最大功能性差距）。

**本审计不含**任何代码修改，也不改变 S1 / F1 / S2 / F12 的既有状态与边界。

---

## 2026-10-21 Commit A / B / C（pump owner · pending edit · selection/status）

三个 slice 各自独立、互不混入。完整实现说明与证据见
**`docs/Editor-Avalonia-Commit-ABC-Closure.md`**。

| slice | 问题 | 结论 |
|---|---|---|
| **A** | production `EngineHost` 后端**没有任何 pump owner** ⇒ UI 从不产生帧 | 唯一 owner = `EditorHostService.BeginRuntime()` 的 `DispatcherTimer`；UI 只消费 `FrameAdvanced` |
| **B** | `PendingEditCompleted` 事件链断裂；UI 把 `accepted` 呈现为"完成" | `Idle/Submitted/Executed/Failed` 四态；成功/失败各恰好一次回执 |
| **C** | 模型选择变更**不发 PropertyChanged**（控件错）；状态栏只在 handler 路径同步 | VM 内部改写选择收敛到 `SetSelectedEntityCore`；`StatusBar` 由 `StatusText` 通知驱动 |

**native inventory：0 增量** ⇒ `test_bridge` 仍 **44**、`test_hrc` **15**、
`test_scripting` **27**，**I2 仍 243**；ABI 仍 **v1.3**（0 个新导出 / 0 个新 op / 0 个新 status）。

**新增 managed gate**：`--selstatus`（`Services/SelectionStatusGate.cs`）+
`tools/run_selection_status_gate.ps1`。C1 在**修复前**即失败
（`model selected #10, list has none`），证明该 gate 确实在测缺陷。

**实测（Debug，native + managed pair 同字节）**：

```text
[PUMP-OWNER GATE] idle:    pumps=28 failures=0 frames=28 rate=18.5/s lastDt=0.048s
[PUMP-OWNER GATE] playing: pumps=27 failures=0 frames=27 rate=17.7/s lastDt=0.048s
[PUMP-OWNER GATE] ALL GREEN (one pump owner, rate<=50/s, attempts == frames + failures)
[PENDING-EDIT GATE] B1/B2: submitted -> executed, exactly one completion
[PENDING-EDIT GATE] B3: submitted -> failed with diagnostic 'asset is not a texture'
[PENDING-EDIT GATE] B4: close unsubscribes and resets the projection
[PENDING-EDIT GATE] ALL GREEN
[SELECTION-STATUS GATE] C1/C3: auto-select reaches the list; status bar tracks the event-driven refresh
[SELECTION-STATUS GATE] C2: one selection change -> one notification (1), same-value reselect -> none
[SELECTION-STATUS GATE] C4: close clears the list selection and the status bar
[SELECTION-STATUS GATE] ALL GREEN
```

native（Debug 与 Release 双配置一致）：`test_bridge` **44/44**、`test_hrc` **15/15**、`test_scripting` **27/27**；
managed：`--pumpowner` / `--pendingedit` / `--selstatus` / `--prodpath` / `--hrc2` /
`--uistate` 均 ALL GREEN + exit 0；`--uistatelegacy` / `--p2frame` / `--p2ui` /
`--d3managed` 断言层 ALL GREEN + 自报 exit=0（进程 linger 为**已记录**的既有现象，
本次未引入、未用 retry 掩盖）。

### 本次首次建立 native pair 时恢复的仓库状态（非本 slice 引入）

工作树中三个 tracked/submodule 目录被删除，导致 native 构建无法完成；已 `git checkout --` 恢复：

1. `third_party/imgui_build/CMakeLists.txt` —— configure 直接失败（`add_subdirectory` 无源目录）；
2. `third_party/JoltPhysics/Build/`（48 文件）—— 物理引擎被**静默跳过**（证据面缩水，非失败）；
3. `engine/{include/Engine/Debug,src/Debug}`（5 对文件）—— `EngineCore` 编译失败（`error C1083`）。

### 两个 staging 缺口（**刻意未修**，仅记录）

| 缺口 | 影响 |
|---|---|
| `OpenAL32.dll` 实际落在 `build/third_party/openal-soft/<Config>/`，csproj 硬编码指向 `build/sandbox/EditorDemo/Debug/` | 宿主报 `Unable to load DLL 'EditorBridge' (0x8007007E)`；需手工拷贝才能运行任何 managed gate（Debug / Release 都一样） |
| `assets/gp01/Main.scene` 被 `.gitignore:88` 排除且无 committed generator | 所有 gate 的 `OpenProject` 失败；需从 `spike_gp07_e3` 恢复（md5 `faabc374…`，10 entities） |

两者属 build/deployment hygiene，属独立 slice 范围。

**Release 双配置复核**：A / B / C 各自新增的 gate 在 Release 下同样 ALL GREEN + exit=0。

```text
[PUMP-OWNER GATE] idle:    pumps=32 failures=0 frames=32 rate=21.1/s lastDt=0.048s
[PUMP-OWNER GATE] playing: pumps=32 failures=0 frames=32 rate=21.3/s lastDt=0.047s
[PUMP-OWNER GATE] ALL GREEN (one pump owner, rate<=50/s, attempts == frames + failures)
[PENDING-EDIT GATE] ALL GREEN
[SELECTION-STATUS GATE] ALL GREEN
```

---

## 2026-10-21 Slice D — Production Viewport Pick / Input

审计见 `docs/Editor-Avalonia-D-Step1-Pick-Production-Audit.md`，
实现与证据见 `docs/Editor-Avalonia-D-Step2-Pick-Production-Closure.md`。

**审计结论**：D 不是"接线"。production 后端上 viewport 输入被 `AcceptViewportInput()`
**整体拦截**（`MainWindow.axaml.cs:593`），且既有 pick 调用点同时用错了三件事：
坐标系（NDC，而 ABI 要 top-left 像素）、时序（同步返回，而 PICK 是 request→帧→fetch）、
身份空间（`Vm.Entities[hitEntity]` 把 `GameObject::ID` 当场景索引）。

**实现**（native / ABI 零改动）：

| 项 | 内容 |
|---|---|
| 坐标契约 | `ViewportPickMapper`：`floor`、半开区间 `[0,W)×[0,H)`、越界**拒绝不 clamp**、非有限值拒绝 |
| 几何前提 | `Stretch=None` + 控件原点锚定；**实测推翻**"控件尺寸==帧尺寸"（窗口较窄时为 554x600，裁剪仍 1:1） |
| 异步语义 | `Idle/Pending/Busy`；受理**不**改选中；`NoHit` 是正常结果；stale-frame 显式区分 |
| 输入所有权 | pointer / keyboard / legacy-capability 三者分离；键盘仅在 viewport 持有焦点时消费 |
| 身份 seam | `IPickIdentityResolver`，默认 `Unavailable` ⇒ `hit-unresolved`，**fail-closed** |
| gate | `--prodpick`（P1~P10）+ `tools/run_production_pick_gate.ps1` |

**新增 gate 的实测（Debug 与 Release 均 ALL GREEN + exit=0）**：

```text
[PROD-PICK GATE] P1: geometry premise holds (stretch=None, source 800x600, laid-out 554x600)
[PROD-PICK GATE] P2: accepted -> Pending, selection unchanged (request #1)
[PROD-PICK GATE] P3: single outstanding request preserved (#1)
[PROD-PICK GATE] P4: hit id=9 is fail-closed (no approved id->index bridge)
[PROD-PICK GATE] P5: hit id=9 at (40,296) -> selected 'Wall_W' (gate-only resolver)
[PROD-PICK GATE] P6: no-hit is a normal result and clears the selection
[PROD-PICK GATE] P8b: HAZARD PROVEN - after deleting index 0, id 9 still resolves to
                      index 8, which is now 'Wall_E' instead of 'Wall_W'
[PROD-PICK GATE] P7: out-of-frame clicks are rejected (never clamped)
[PROD-PICK GATE] ALL GREEN
```

**P8b 是本 slice 最重要的证据**：它把"为什么不能用 `id - 1` 当生产映射"做成可执行证明。
在未删除实体的加载态下 `ID == index + 1` 恰好成立（`s_NextID` 从 1 起、对象按装载顺序 append），
因此该映射**看起来是对的**；删除 index 0 后，**同一个像素**的 id 仍是 9 ⇒ `id-1` 仍是
index 8，而索引 8 已变成 `Wall_E`（原为 `Wall_W`）。静默选错邻居，无任何报错。

**回归**：`test_bridge` **44/44**、`test_hrc` **15/15**、`test_scripting` **27/27**、
`test_pick` **8/8**（Debug + Release）；`--prodpick` / `--pumpowner` / `--pendingedit` /
`--selstatus`（Debug+Release）与 `--prodpath` / `--hrc2` / `--uistate` / `--p2frame` /
`--p2ui` / `--d3managed` / `--uistatelegacy`（Debug）全 ALL GREEN。

**边界**：ABI 仍 **v1.3**、`PICK=29` / `OP_COUNT=30` / packet 32B 未变、**0** 个新导出、
**0** 个新 op/status、**0** 个新 native 测试（**I2 仍 243**）、fixture 未写
（`Main.scene` md5 `faabc374…`，10 entities）。

### 阻塞项：identity bridge（待批，未实现）

`PickResultPacket.entity_id` 是 `GameObject::ID`，UI 需要场景索引，二者**无 ABI 通路**。
建议方案 **P-1**：把既有 `reserved` 正式定义为 `entity_index`
（native 在 `ServicePickRequestForFrame` 内用 `Scene::FindByID` 反查下标）+ ABI `v1.3 → v1.4`。
它**不增导出、不增 op/status、packet 仍 32B**，但**是一次真实契约变更**，需单独批准。
落地后只需改 `PickIdentity.cs`；gate P5 已是其验收用例。

### 附带发现：gate flag 子串匹配（既有隐患，未修）

`Environment.CommandLine.Contains("--x")` 是子串匹配。本 slice 最初用 `--prodpathpick`，
因包含 `--prodpath` 而使两个 gate 同时注册、互相打断（已改名 `--prodpick`）。
同样的机制意味着既有 `--uistatelegacy` 会连带注册 `--uistate`；实测未产生假绿，
但建议后续改为 argv 精确匹配（独立 slice）。

---

## 2026-10-21 Slice D Step 3 — Identity Bridge（ABI v1.3 → v1.4）

完整说明见 `docs/Editor-Avalonia-D-Step3-Identity-Bridge-ABI-v1.4.md`。

**变更**：`PickResultPacket` 的 offset 28 从"必须为 0 的 `reserved`"正式变为
**`entity_index`**；native 在 `ServicePickRequestForFrame` 内用 `Scene::FindByID`
把 `GameObject::ID` 解析为**该帧快照**的场景索引，无法解析时写
`ENGINE_HOST_PICK_INDEX_NONE (0xFFFFFFFF)`。**32 字节布局不变**，
`RuntimeResult` 仍 76B，**0** 个新导出 / **0** 个新 op / **0** 个新 status。

**为什么必须升版本**：v1.3 的契约是"`reserved` 必须为 0"（managed 解码器曾显式断言），
改用该字段是**真实契约变更**，不是新增字段。升到 v1.4 后两边都是等值校验，
因此 v1.3↔v1.4 任一方向的混搭都在 `EngineHostChannel.Create()` 处失败，
并由 M1 / M8-B / `test_bridge` 三重捕获。

**哨兵不是 0**：0 是合法的第一个场景索引，用它当哨兵会让"第一个实体"与
"无法解析"混同。

### 永久回归（P8b 固化）

`tests/test_bridge/test_pick_transport.cpp`：
`D3PickEntityIndexTracksIdentityAcrossDeletion` —— pick → 删索引 0 → 同像素再 pick：
`entity_id` 不变、`entity_index` **前移一位**、名字仍指向同一实体。
`D3PickIdentityResolutionFailsClosed` —— 无场景 / id 0 / 未知 id ⇒ 恒 NONE。
`D3PickPacketLayoutIsStableInV14` —— 32B + offset 0/4/8/16/24/28 + 哨兵值。

`--prodpick` P8b（managed，真实 UI 路径）：

```text
[PROD-PICK GATE] P5:  hit id=9 -> index 8 'Wall_W' (production resolver, no injection)
[PROD-PICK GATE] P8b: identity regression OK - same pixel, same entity 'Wall_W',
                      index 8 -> 7 (id 9 unchanged)
[PROD-PICK GATE] P9:  v1.4 contract verified (index used verbatim, NONE refused,
                      non-HIT+index rejected)
[PROD-PICK GATE] ALL GREEN
```

Step 2 的旧 P8b 证明"`id - 1` 会选错"（`Wall_W` → `Wall_E`）；新 P8b 证明
"新字段不会选错"。同一场景、同一像素，断言方向从危害翻转为正确。

### 计数

| 项 | 值 |
|---|---|
| ABI | **v1.4**（`(1<<16)\|4`） |
| `test_bridge` | 44/44（未变） |
| `test_pick` | 8/8（未变） |
| `test_pick_transport` | 6 → **9**（**不计入 I2**，按既有规则） |
| **I2（门禁校验）** | **251 不变** |
| `--prodpick` / `--d3managed` / `--pumpowner` / `--pendingedit` / `--selstatus` | Debug + Release 全 ALL GREEN + exit=0 |
| `--p2frame` / `--p2ui` / `--prodpath` / `--hrc2` / `--uistate` | Debug 全 ALL GREEN |

fixture 未写：`Main.scene` md5 仍 `faabc37448f44197d6966708532c1ef9`（10 entities）；
native 用例的删除只作用于内存会话。

### P-1 的 I2 实跑（integrity gate，`-SkipBuild`，Debug）

```text
  ok    test_bridge     44 tests
  ok    test_hrc        15 tests
  ok    test_pick        8 tests
  ok    test_scripting  27 tests
```

同一跑次报出 **7 项既有失败**（本 slice 未触碰）：`test_content` 42/60、`test_core` 20/43、
`test_renderer` 9/14 的**基线漂移**，以及 `DF03/DF05/DF06/DF07` 依赖的 gitignored
dogfood fixture 缺失。**与 P-1 无关，但不应被 P-1 的全绿掩盖**；属 R1/R2 同类问题面。

---

## 2026-10-21 Slice E Step 1 — 动态 Viewport 尺寸审计（docs-only，零代码）

完整审计与契约冻结见 `docs/Editor-Avalonia-E-Step1-Viewport-Size-Audit.md`。

**零代码**：未修改 native / managed / shader / ABI / gate / fixture；ABI 仍 **v1.4**，
`PICK=29`、`OP_COUNT=30`、packet 32B 均未变。

### 审计结论（逐条可核验）

| # | 结论 | 证据 |
|---|---|---|
| E-A1 | resize **完全未接线** | `editor_avalonia/` 中 `SizeChanged` 命中 **0** |
| E-A2 | presentation FBO 只在 bootstrap 创建一次 | `EngineHost.cpp:557`（`Resize(800,600)`，此后无调用） |
| E-A3 | fetch 对 FBO 尺寸做**等值**校验 | `EngineHost.cpp:2262` |
| E-A4 | packet header **已带** `width/height` | `capi.h:495-497` 的 16B header 布局 |
| E-A5 | 容量常量同时是"尺寸"与"容量" | `ENGINE_HOST_FRAME_PACKET_BYTES = 16 + 800*600*4` |
| E-A6 | 渲染/pick 几何**已尺寸无关** | `RenderProductionFrame` 每帧按 aspect 重算相机；`PumpOneFrame` 按 FBO 设 viewport；`ReadPickPixel` 按 FBO 判越界 |
| E-A7 | managed 解码要求精确长度 + 精确 800×600 | `PresentationFrame.TryDecode` |
| E-A8 | mapper 入参用编译期帧尺寸 | `MainWindow.RequestPickAtPoint` |
| E-A9 | **每帧**分配 1.92 MB fetch 缓冲 | `EditorHostService.cs:970`（30 Hz ≈ 58 MB/s；容量放大后 ≈ 250 MB/s） |
| E-A10 | `OpenGLFramebuffer::Resize` 静默 `max(w,1)` 且**先 Destroy 再创建** | `OpenGLFramebuffer.cpp:103-107` |
| E-A11 | EngineHost **无** resize 通道（legacy 的 `EditorSession_SetViewportSize` 属禁止路径） | `EditorHostService.cs:487` 为 `LegacyOnly` |
| E-A12 | "无新帧" 判定基于 `m_FrameId == m_LastFetchedFrameId` | `EngineHost.cpp:2273` |
| E-A14 | `test_bridge` 断言 `PresentationTargetWidth() == 常量` | `test_editor_capi.cpp:1762` |

### 待批 ABI 决策（E 的唯一阻塞项）

EngineHost 侧**没有任何尺寸输入通道**；复用既有 op / `PumpOneFrame(dt)` 均不可行。
唯一合理选项是 **append-only 新 op**：

```text
ENGINE_HOST_OP_SET_VIEWPORT_SIZE = 30      // OP_COUNT 30 -> 31
floatArg[0] = width, floatArg[1] = height
ABI v1.4 -> v1.5；新 export 0 / 新 status 0 / packet 字段 0
```

容量常量需拆分为"默认尺寸（仍 800×600）/ 最大尺寸 / 分配容量"，
`FRAME_PACKET_BYTES` 语义从"帧大小"变为"最大容量"——这是真实契约变更，
必须升版本，否则 v1.4 managed 与 v1.5 native 会在**运行时**才失败而不是在 `Create()` 被抓住。

### 冻结的关键契约（摘要）

- **E-C1** 三尺寸分离：FBO 尺寸（渲染真相）/ 帧尺寸（packet header）/ 控件尺寸（仅算显示矩形）。
- **E-C2** 解码的尺寸唯一来源是 header；`ExpectedWidth/Height` 降级为默认值。
- **E-C3/C4** mapper 增加 displayed rect + frame size 显式入参；负/零/非有限/不可逆变换一律拒绝；**不 clamp**。
- **E-C5** "裁剪"≠"缩放"：控件小于帧仍按 1:1 映射（D Step 2 已实测控件 554×600 vs 帧 800×600）。
- **E-C6** resize 生命周期：last-wins 合并 → 登记 → 下一帧 current-context 窗口内应用 → 新 `frame_id`；
  零尺寸与超限**拒绝**（不 clamp）；重建失败**不发布帧**（fail-closed，不混用）。
- **E-C7** 与 P-1 衔接：沿用 `frame_id`，不新增 generation；**同帧 resize 必须由 native 丢弃 pending pick**
  （managed 的 `frame_id < presented` 判定不会拒绝它，因为它带的是新 frame_id）。
