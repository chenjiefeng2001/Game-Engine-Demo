# NHI-1 — Native Hang Investigation（结论：根因已定位）

> **切片类型**：`Native Investigation`（**A2 外围**，不重开 A2）
> **立项日期**：2026-09-27 · **收口**：2026-09-28
> **状态**：`CLOSED / Closure B`（根因已识别 + 受控对比完成 + 第三方基础设施条件已隔离）
> **触发来源**：Stage A/A2 的负例跑批（`docs/Editor-Avalonia-HRC2-StageA-A2-Closure.md` §4）
> **关系**：A2 的 F-2 功能门禁全绿；本 finding 属**测试环境/工具链交互**，**不是产品缺陷**
> **证据链**：`docs/Editor-Avalonia-HRC2-NHI-1-Log.md`（E-001 ~ E-011，含历史记录更正）

---

## 0. 结论（一句话）

> 进程在 `main` 返回后的 **`LdrShutdownProcess` → FLS 回调**阶段，
> 被全局注入的 **NVIDIA ShadowPlay overlay（`nvspcap64.dll`）** 回调调用 `free()`，
> 而该 `free()` 被 **MSVC ASan 分配器拦截**接管，进入 ASan TLS 初始化后在
> `WaitOnAddress` 上自旋死锁 → **进程永远无法完成退出**。

**与 EngineHost / GLFW / OpenGL / EngineHost teardown 无关。** A2 §3.3 修掉的
`Shutdown` 未完成仍 `Destroy`（late-free）是**另一个**缺陷，已保持关闭状态。

完整 native 栈、受控对比与全部实测数据见 `docs/Editor-Avalonia-HRC2-NHI-1-Log.md`（E-007 ~ E-010）。

---

## 1. 必要条件（本机环境）

| # | 条件 | 说明 |
|---|---|---|
| 1 | 进程被注入 `nvspcap64.dll` | NVIDIA ShadowPlay / GeForce Experience overlay，全局注入 |
| 2 | 进程加载 **ASan 插桩**的 `EditorBridge.dll` | 即 Debug 构建；Release（非 ASan）不触发 |

**受控对比**（同一 probe、同一异常拆除路径）：

| 被测桥接 DLL | 次数 | 挂起 | 退出阶段 avg |
|---|---|---|---|
| Debug（ASan） | 12,225 | **37（约 0.3%）** | 600–900ms（长尾至 1.8s） |
| Release（非 ASan） | 400 | **0** | **78ms** |

---

## 2. 本 slice 的四条工作线结论

| 线 | 结论 |
|---|---|
| NHI-1.1 抓栈 | **DONE**（cdb `-pv` 非侵入式 attach，进程已停滞 4 小时） |
| NHI-1.2 三路径对比 | **DONE**：根因落在三者**共同的进程退出阶段**，不是某条 teardown 路径特有 |
| NHI-1.3 Shutdown/Destroy 顺序 | **DONE**：两种 policy 均非成因；A2 §3.3 缺陷保持已关闭 |
| NHI-1.4 native-only 二分 | **DONE**：去掉 CLR/Avalonia/managed Host 仍复现 ⇒ 调查面收窄到 native 退出路径 |

## 3. 明确没有做的事

- 未修改任何 native / teardown 代码（native diff = 0）
- 未用 watchdog 把该问题"测试绿化"（watchdog 只在 harness 里回收进程，timeout 一律记 FINDING）
- 未改动 A2 / A6 / A7 契约，未吸收 A3 项与 4 条 Phase 7 WIP warning
- A8 继续 DEFERRED

## 4. 工具与仓库约定（本次沉淀）

| 项 | 内容 |
|---|---|
| `tools/nhi1_stress.ps1` | managed 侧（`--boot` / HRC-1 gate）压测 + 挂死取证 |
| `tools/nhi1_native_teardown_probe/` + `tools/nhi1_native_probe.ps1` | native-only 8 场景 probe（不进 CTest、不改 I2=240） |
| DONE-then-stall detector | probe 打印 `DONE` 后若 N 秒未退出即视为 hang 前兆，**先 attach 再回收** |
| cdb | `winget install Microsoft.WinDbg`；**必须从 `%TEMP%` 副本运行**（WindowsApps 包目录不可直接执行）；用 `-pv` 非侵入式 attach |
| **仓库约定 1** | 含 CJK 的 `.ps1` **必须带 UTF-8 BOM**（PS 5.1 否则按 ANSI 解码致解析错位） |
| **仓库约定 2** | native C++ 若包含无 BOM 的 UTF-8 头（如 `capi.h`），**必须 `/utf-8`**（与主工程 `CMakeLists.txt:39` 一致） |
| **仓库约定 3** | 进程存活判定**不得**依赖 `Start-Process -PassThru` 的 `HasExited`（PS 5.1 不可信，本次造成 12,225 runs 假阴性）；改用 .NET `Process` API |
| **环境约束** | 需 `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1` 才能直跑 ASan 测试（既有纪律，`tools/run_test.cmd`） |

## 5. 处置（已裁决落地：① + ③；② 作为证据分层策略保留）

| # | 处置 | 状态 |
|---|---|---|
| ① | **runner 排除 `nvspcap64.dll` 注入**（首选环境修正）：在跑 ASan 测试的机器/runner 上禁用 NVIDIA ShadowPlay / GeForce overlay 注入。**验证方式**：被测进程模块列表中不得出现 `nvspcap64.dll` | 已裁决；**尚未在自动化中实现**（作为 runner 前置条件记录） |
| ③ | **登记为测试基础设施约束**（永久记录，不得省略）：本机全局 NVIDIA overlay 注入 + MSVC ASan 桥接 DLL 组合会在**进程退出 FLS 回调**阶段产生第三方死锁；**不归因于** EngineHost / GLFW / GL teardown | 已落地（§4 + Log E-011） |
| ② | **证据分层策略（保留，不替代）**：Debug + ASan = 功能 / 内存安全 / 生命周期负例的**主要**证据；Release / non-ASan = **进程退出稳定性**这一维度的**独立**证据；Release 结果**不能**替代 ASan 的功能与内存检测价值 | 已冻结（§4 + Log E-011.2） |

**明确声明**：这不是产品修复，也没有做过"禁 overlay 后跑 N 次即宣布修复"的动作。
正确结论是：**根因已识别 + 受控对比完成 + 第三方基础设施条件已隔离**。

## 6. 关闭与去向

```text
NHI-1   CLOSED / Closure B
A2      IMPLEMENTED / 7-7 GREEN；NATIVE TEARDOWN FINDING → CLOSED
native  零修改（EngineHost / GLFW / teardown 均未触碰）
I2      240 未变（probe 不进 CTest）
A8      继续 DEFERRED
next    Stage A / A3（MainViewModel refresh 路径收口：IsOpen 单一事实源、create+auto-select 竞态、
        handler 缺 create 结果/失败行）
```
