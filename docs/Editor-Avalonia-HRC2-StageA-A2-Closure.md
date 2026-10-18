# Editor Avalonia HRC-2 阶段 A / A2 — F-2 Bootstrap Trigger 实现与证据

> **阶段**：UI-HRC-2-A / Stage A / A2（**step 2：实现 + 负例证据**）
> **日期**：2026-09-26
> **状态**：`IMPLEMENTED / 7-7 SCENARIOS GREEN —— 但 BLOCKED：1 个 native 退出缺陷未闭环`
> **前置**：`docs/Editor-Avalonia-HRC2-StageA-A2-Trigger-Contract.md`（F2-01~F2-10 已 ratify）
> **native 改动**：**0**（`capi.h` / `EngineHost.*` / `test_hrc` / `test_bridge` 未触碰）
> **不吸收**：A3 的 create/auto-select 竞态、handler 缺 create 结果行、4 条 Phase 7 WIP warning

---

## 1. A2-F2 门禁结果

证据入口：`AvaloniaEditor.exe --boot`。场景由**进程启动时**已存在的 `EDITOR_BRIDGE_FAIL_*`
决定（MSVC `getenv` 读 CRT 启动快照，.NET 运行期改环境变量对 native 不可见 → 每个注入场景一个进程）。

| A2 门禁 | 关闭的 contract | 证据场景 | 结果 |
|---|---|---|---|
| trigger contract | F2-02 / F2-03 / F2-04 | `Happy`：启动期 `Starting`+`!HasSession`+`frames==0`+`glfwInit==0` → Open → `Ready` | PASS |
| ownership contract | F2-01 | 唯一 `EnsureRuntime()`（旧名 `CreateSession` 已删除，gate 调用点同步改名） | PASS |
| startup negative | F2-03 / F2-06 | 启动期点 `+ Entity` 被拒且不 bootstrap；bootstrap 期间同步阻塞不重入 | PASS |
| failure propagation | F2-05 | 3 个 bootstrap 注入 + frame/tick/play 注入，结构化诊断到达 Console/状态栏/ScriptError | PASS |
| reopen negative | F2-07 / F2-10 | CloseProject 后 `HasSession==true`、`glfwInit` 计数**不增长**、pump 停止；重开复用同一 runtime | PASS |
| dispose / no-resurrection | F2-08 | 终态后 `EnsureRuntime/OpenProject/GetCounts` 全部显式拒绝；点击 Open 只产生可观测拒绝 | PASS |
| legacy compatibility | F2-09 | 两个 backend 共用命名触发点与失败形态；retention 差异各有断言（legacy 销毁 / EngineHost 保留） | PASS |
| A1 regression | — | `--prodpath` / `--hrc2` / `--selftest` / `--gate1..3g` / native 全量 | PASS（见 §5） |

7 个场景逐一（每个都是独立进程）：

```text
Happy            ALL GREEN    exit 0
BootstrapAppInit ALL GREEN    exit 0     InitFailed / EngineSubsystem
BootstrapFbo     ALL GREEN    exit 0*    GLFailed / FBO
BootstrapSession ALL GREEN    exit 0*    SessionFailed / Session
FrameFault       ALL GREEN    exit 0     TickFault / Tick（pump 检出）
TickFault        ALL GREEN    exit 0     TickFault / Tick（运行中第一帧）
PlayBindFault    ALL GREEN    exit 0     SessionFailed / Play（可恢复，runtime 仍可用）
* 这两个场景的**断言**全绿，但进程在 Environment.Exit 之后偶发不退出 —— 见 §4。
```

---

## 2. 实现清单（managed-only）

| # | 改动 | 位置 | 关闭 |
|---|---|---|---|
| S1 | `EnsureRuntime()`：bootstrap 的唯一命名触发点（幂等；删除 `CreateSession`） | `EditorHostService.cs` | F2-02/04 |
| S2 | `HostOperationException`：搬运 native `status`/`stage`/`diagnostic`（`StatusText`/`StageText` 映射表） | 同上 | F2-05 |
| S3 | `HostRuntimeState.Faulted`：runtime 不可用但未终态；`RequireRuntimeAvailable` 显式拒绝 | 同上 | F2-05/08 |
| S4 | pump 检出 typed failure → 停泵 + 置 `Faulted` + `RuntimeFaulted` 事件 + `LastRuntimeFailure` | 同上 | F2-05/06 |
| S5 | `Faulted` 时的 `CloseProject` 不再向已自毁的 native host 提交命令 | 同上 | F2-07/10 |
| S6 | **`Shutdown` 未完成则不 `Destroy`**（保留句柄并上报） | 同上 | §3.3 |
| S7 | `MainViewModel.TryHost/ReportHostFailure/OnRuntimeFaulted`：失败 → Console + 状态栏 + ScriptError，**不外抛** | `MainViewModel.cs` | F2-05 |
| S8 | refresh 路径（collections / inspector / camera）失败容忍，不再把异常抛给 View | 同上 | F2-05 |
| S9 | 非 `Ready`（含 `Faulted`）时的事件消费显式忽略并记录 | 同上 | F2-05 |
| S10 | `MainWindow.GuardedUiAction`：View 边界最后一道网，**只拦 host 契约内的失败类型**（真实编程错误仍然抛出） | `MainWindow.axaml.cs` | F2-05 |
| S11 | `GateProbe`（共享 UI 驱动/断言/未处理异常探针）+ `BootstrapTriggerGate` + `--boot` | 新增 | 全部 |
| S12 | 只读诊断：`GlfwLifetime`（F2-07 的直接证据） | `EditorHostService.cs` | F2-07 |

**一处故意的契约变更（需记录）**：`Hrc1Gate` 原先断言 `vm.OpenProject` 在 disposed host 上
**抛出** `ObjectDisposedException`。F2-05 ratify 后该逃逸被取消，断言改为：
VM 返回 `false` + 显式上报，且**不再**抛出；host 层 `EnsureRuntime()` 抛 `ObjectDisposedException`
的原断言保留。契约意图（disposed host 不可复活）未变，表达形式从异常改为可观测状态。

---

## 3. 过程中发现并当场修复的 3 个真实缺陷

### 3.1 bootstrap 失败以 `TargetInvocationException` 逃出 XAML handler（A1 实验已捕获）

- 旧行为：真实诊断（`status=6/stage=6/diagnostic`）被反射派发包裹丢失，用户只看到一句
  "Exception has been thrown by the target of an invocation."
- 修复：S2/S7/S10。修复后同一注入场景的 UI 得到
  `open project failed: SessionFailed / Session — ...`，进程 exit 0、无未处理异常。

### 3.2 pump 的 typed failure 被完全忽略

- 旧行为：`PumpOnce` 丢弃 `EngineHost_PumpOneFrame` 的返回码与 `RuntimeResult`，
  对着一个已 fault 的 host 继续每 33ms tick 一次，且 managed 状态永远停在 `Ready`。
- 修复：S4。修复后：停泵、`State=Faulted`、`LastRuntimeFailure` 结构化记录、UI 停止假装在运行、
  后续操作显式拒绝（`FrameFault` / `TickFault` 两个场景即其证据）。

### 3.3 `Shutdown` 未完成仍然 `Destroy`（late-free）

- 触发：A2 的 `TickFault` 场景偶发 `0xC0000374`（堆损坏，无 ASan 报告、无 crash report）
  与一次"gate 全绿但进程不退出"。
- 根因（读码确认）：`EngineHost::Shutdown` 对 `FAULTED` 主机**恒返回 -1**
  （"host is already faulted"，`EngineHost.cpp:1259-1286`）；而 managed 旧代码忽略该返回码，
  直接 `EngineHost_Destroy` → `delete host` → 析构里再跑一次 `Shutdown` + join，
  超时则 `std::terminate()`（`EngineHost.cpp:117-130`）。在 runtime 线程生死未定的状态下
  `delete host` 就是 late-free。
- 修复：S6。`Shutdown` 必须是 `ShutdownComplete` 才 `Destroy`，否则保留句柄 + 上报。
- 证据：修复前 tick 场景 20 次里 1 次挂死（另有一例堆损坏）；修复后 **50 次全绿**（20 + 20 + 10）。

### 3.4 实测事实修正（不是缺陷，是被证伪的假设）

`EDITOR_BRIDGE_FAIL_PLAY_BIND` **不会**让 host fault：异常在 `EditorSession::Play` 内部被
rollback 并转成 `Fail(...)`，因此这是**可恢复失败**——host 保持 `Ready`、pump 继续、
可重试、native 诊断文本（"play: runtime initialization exception: ..."）完整到达 UI。
第一版 gate 假设它会 fault，断言被实测推翻后按事实改写。

---

## 4. 未闭环的 1 个发现（BLOCKER，需你裁决）

**症状**：gate 全部断言通过、`exit=0` 已打印之后，进程偶发**不退出**（`Environment.Exit(0)` 之后卡住）；
另有一次 `0xC0000374` 堆损坏（修复 §3.3 之前）。复现时进程仍存活：35 线程、7.6s CPU、716MB WS，
内存充裕（42.5GB free）、无残留 editor 进程。

**已排除的原因**：

| 假设 | 证据 | 结论 |
|---|---|---|
| A2 的 F2-05 改动引入 | `--hrc1` 在**恢复旧的抛异常行为**后仍以同频率挂死（6/12） | 排除 |
| ASan 特有 | 换成 **Release**（非 ASan）`EditorBridge.dll` 反而更频繁（11/15） | 排除 |
| 特定 gate | `--gate1` 12/12 干净，`--hrc1` 5/12 挂死；同一批里 `gate1/3a/3b/3d` 也开始挂 | 与具体 gate 无关，环境相关 |
| 资源耗尽 / 僵尸进程 | 无残留进程，空闲内存 42.5GB | 排除 |
| 正常路径 | `--prodpath` / `--hrc2` / `--selftest` / `--boot Happy` 反复干净 | 只在**异常拆除**路径出现（bootstrap 回滚 / faulted 紧急拆除 / 未打开工程就销毁 legacy session） |

**性质**：这是 native 侧"异常拆除后进程退出"路径的缺陷（GL/GLFW/driver/loader-lock 层面），
落在 A2 冻结的 native 范围之外。A2 的高强度负例跑批把它暴露了出来（此前没有任何 managed gate
把 bootstrap 失败跑到进程结尾），但它**不是 A2 引入的回归**。

**建议**（需你选）：
1. **开独立 native slice** 定位（推荐）：`Shrink` 复现（`--boot` + `FAIL_FBO` 循环）+ 线程栈
   （需装 `dotnet-stack`/`procdump`）+ 定位 loader-lock/driver detach 根因。
2. **接受为已知环境缺陷**：给 runner 加 watchdog（超时即 kill 并记为"exit hang"，不计入 gate 结论），
   同时在 ledger 登记，等有 Linux/干净机环境时再复现。
3. 先不动，A2 以 `IMPLEMENTED / 7-7 GREEN + 1 OPEN FINDING` 结项，把该发现挂到 A8 式的后置项。

在 1/2/3 未定之前，**A2 不标 CLOSED**（按 §11 Definition of done 与 plan 的 stop condition：
"failure path 留下 live handle/callback/timer" 属于必须回审计的情形）。

---

## 5. 回归结果

```text
A2 场景         7/7 ALL GREEN（BootstrapFbo / BootstrapSession 的断言绿，进程偶发不退出 = §4）
A1 回归         --prodpath ALL GREEN（两次）；--hrc2 ALL GREEN；--selftest ALL GREEN
HRC-1 gates     --gate1/2/3a/3b/3c/3d/3g 断言 ALL GREEN（其中 4 个在最后一批出现 §4 的退出挂死）
--hrc1          断言 ALL GREEN（新契约断言生效），进程退出挂死率 ~30-40%（§4，pre-existing）
native          test_hrc 15/15 · test_bridge 41/41 · test_scripting 27/27（Debug + Release）
integrity gate  I1 + I2 + I3 ALL GREEN，in-scope total = 240（未变）
managed build   0 errors / 4 warnings（4 条均为既有 Phase 7 viewport WIP baseline）
fixtures        assets/gp01/{manifest.json,Main.scene,game.lua} MD5 与 A1 完全一致
residual        targeted process = 0；git diff --check exit 0
```

---

## 6. 明确不做（延续）

- `SaveProject` / `ScriptSave` 落盘闭环（会写 repo fixture）
- Viewport / Picking / Gizmo / Renderer / ECS / `test_ecs` / Linux receipt（A8 仍 DEFERRED）
- A3 项：`create + auto-select` 竞态、handler 缺 create 结果/失败行
- 4 条 Phase 7 WIP warning
- `IsOpen` 单一事实源（fault 后 VM 本地集合与 host 状态分歧 —— 已记录，归 A3）

---

## 7. 当前判定

```text
F2-01 ~ F2-10 契约:            FROZEN + IMPLEMENTED（7/7 场景 GREEN）
A2 三个真实缺陷:               已修（3.1 / 3.2 / 3.3）
HRC-1 契约:                    1 处断言按 F2-05 改写（意图不变）
native 改动:                   0
A2 判定:                       BLOCKED —— §4 的退出挂死/堆损坏未闭环
next action:                   由你裁决 §4 的 1 / 2 / 3
```
