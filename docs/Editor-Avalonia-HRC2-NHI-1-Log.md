# NHI-1 调查日志 — Native Hang Investigation

> 记录按时间追加的**实测数据**。结论只在证据支持时升级；假设必须显式标注为假设。
> 切片定义与结论摘要见 `docs/Editor-Avalonia-HRC2-NHI-1-Charter.md`。
> **状态**：`CLOSED / Closure B`（2026-09-28）
>
> ⚠️ **历史记录更正（详见 E-011）**
> - E-008 的"批准批次 x80 / 针对性批次 x60 无命中"**作废**，不能作为复现率证据；
> - 长批次日志的 `findings=0 / 12,225 runs` **作废**（harness 缺陷造成假阴性，见 E-009.1）；
> - 有效证据只有四类：**OS 级残留进程计数**、**native stack**、**受控对比**、**分层策略结论**。

---

## E-001 · 2026-09-26 · A2 负例跑批首次捕获

- 现象：`--boot`（TickFault 场景）20 次里出现 1 次**进程 hang**；另一次 `0xC0000374`（堆损坏，
  无 ASan 报告、无 crash report）。
- 关键定位：hang 发生时 gate 已打印 `ALL GREEN` 与 `boot exit=0`，即**所有断言通过、退出码已定**。
- 同批数据：`--hrc1` 5/12 hang；`--gate1` 12/12 干净；换 Release DLL 后 `--hrc1` 11/15 hang。

## E-002 · 2026-09-26 · 归属排除实验（A2 改动是否相关）

| 实验 | 手段 | 结果 |
|---|---|---|
| 恢复 F2-05 前的抛异常行为 | 临时 env 开关 `A2_TEMP_ESCAPE=1` | `--hrc1` 仍 6/12 hang → **与 A2 无关** |
| ASan 假说 | 把 Release `EditorBridge.dll` 放进独立 bin 目录跑同一 gate | 11/15 hang（**更频繁**）→ 不是 ASan 特有（**该结论已在 E-010 被修正/细化**） |
| 资源假说 | 内存/进程普查 | 42.5GB 空闲、无残留 editor 进程 → **排除** |
| 路径相关性 | 正常路径 gate 反复跑 | `--prodpath` / `--hrc2` / `--boot Happy` 干净 → 只在**异常拆除**路径 |

**判定**：既有 native 缺陷，被 A2 首次跑到进程结尾而暴露。

## E-003 · 2026-09-26 · A2 §3.3 修复（独立缺陷，保持关闭）

- 读码确认：`EngineHost::Shutdown` 对 `FAULTED` 主机**恒返回 -1**（`EngineHost.cpp:1259-1286`），
  managed 旧代码忽略该返回码直接 `EngineHost_Destroy` → `delete host` → 析构内二次
  `Shutdown` + join，超时则 `std::terminate()`（`EngineHost.cpp:117-130`）。
- 修复：`Shutdown` 必须 `ShutdownComplete` 才 `Destroy`，否则保留句柄并上报。
- **本条与 NHI-1 的 hang 是两个不同 finding，不得合并**（E-009 已证明 hang 与 teardown 无关）。

## E-004 · 2026-09-27 · 专用压测 harness（`tools/nhi1_stress.ps1`）

- 能力：场景化注入、逐次 exit/verdict、hang 时线程来源普查 + 托管栈 + minidump。
- 工程坑（已修）：脚本无 UTF-8 BOM + CJK 注释 → PowerShell 5.1 按 ANSI 解码导致解析错位
  （`$psi.FileName` 报"属性不存在"）。**仓库约定 1** 由此固化。

## E-005 · 2026-09-27 · 压测数据（hang 率随负载波动）

| 批次 | 场景 / gate | 次数 | hang | 备注 |
|---|---|---|---|---|
| 1 | `--boot` + `FAIL_FBO` | 25 | 2 | 两次均 `+120s` 仍未退出 |
| 2 | `--boot` + `FAIL_FBO` | 18 | 1 | 同上 |
| 3 | `--boot` + `FAIL_FBO` | 10 | 1 | 早期手动循环 |
| 4-5 | `--boot` + `FAIL_FBO` | 20 / 15 | 0 | **作废**（检测器盲，见 E-009.1 / E-011） |
| 6 | `--hrc1` | 14 | 1 | 采集中途脚本异常退出，census 缺失 |

**判定**：hang 率在同一 binary 上波动，**与机器负载/时序强相关**。

## E-006 · 2026-09-27 · 活体 hang 进程的取证

```text
线程数        35–37        工作集  679–716 MB        CPU  9–16 s
dotnet-stack  失败：Pipe is broken（对存活进程，诊断 IPC 无法应答）
comsvcs dump  ThreadCount: 4，Hosted Runtime: no
```

**当时的解读（假设）**：CLR 已完成 shutdown → hang 在进程退出 / DLL detach 阶段
（loader lock 或 driver 线程）。**该假设在 E-009 被 native stack 直接证实并精确化。**

## E-007 · 2026-09-27 · NHI-1.4：native-only 也复现（决定性二分）

工具：`tools/nhi1_native_teardown_probe/`（独立 CMake 工程，**不进 CTest / 不改 I2=240 / 不新增 ABI**）
+ `tools/nhi1_native_probe.ps1`。probe 是**极薄 driver**，只调用现有 C ABI：
`RegisterMainThread → EngineHost_Create → (OpenProject → Play →) PumpOneFrame → Shutdown → Destroy`。
8 个 scenario（`A-fbo/appinit/session`、`B-tick/frame-managed/legacy`、`C-legacy-session`）。

**复现记录**（首轮 `A-fbo`）：退出阶段耗时逐 run 增长
`0.15–0.5s → 2.48s → 2.38s → 7.40s → 2.60s`，第 25 次 **FINDING HANG**
（threads=1, cpu=2.4s, ws=40MB），而 probe 自身内部 total 始终只有 ~1.0–1.5s。

**二分结论**：

| Native-only | Managed/Avalonia | 结论 |
|---|---|---|
| **Hang** | Hang | ✅ native 侧高度可疑 → 继续 native investigation |

即去掉 CLR / Avalonia / managed Host 后 hang 依然存在。

## E-008 · 2026-09-27 · NHI-1.1 抓栈尝试 ⚠️ **本节的"无命中"结论已作废**

### 8.1 工具就绪（过程中修掉的 6 个 harness/工具缺陷）

| 缺陷 | 现象 | 处置 |
|---|---|---|
| `$pid` 只读自动变量 | `Cannot overwrite variable PID` | 改名 `$procId` |
| 参数块被 patch 出重复字符 | 默认路径变成 `Debug\nnhi1_...` | 显式重写 + char codes 校验 |
| `ReadLineAsync` 行泵在 PS 5.1 拿不到尾部 | `DONE` 行丢失 | 改为文件重定向 + 轮询 tail（**后又因 E-009.1 回退**） |
| `cdb -c` 用数组传参 | `CommandLine: q` / 0n2 | `-c` 必须单串传参 |
| `cdb.exe` 在 WindowsApps 包目录 | `Access is denied` | 复制到 `%TEMP%` 后执行 |
| hang 分支先读流后 kill | **harness 自身死锁** | 先 Kill 再限时收输出；attach 仍在 Kill 之前 |

### 8.2 DONE-then-stall detector

probe 最后一行 `DONE rc=.. total=..ms` 之后即进程退出阶段；`DONE` 后 N 秒未退出 → 判 STALL，
**先 attach（`~*k` → `~*kv` → `.dump /ma`）再回收**。健康基线：exit-phase avg ≈ 0.6–0.7s。

### 8.3 ⚠️ 作废的"无命中"记录

| 批次 | 场景 | 次数 | 报告的 STALL/HANG | 裁定 |
|---|---|---|---|---|
| 批准批次 | A-fbo | 80 | 0 | **作废**（检测器盲） |
| 针对性（fresh DLL） | A-fbo | 60 | 0 | **作废**（检测器盲） |
| 长批次 | A-fbo | 12,225 | 0（`findings=0`） | **作废**（检测器盲；同期 OS 里有 37 个活体） |

原记录为 `NHI-1.1 capture attempt: no live-hang attach opportunity`。
**更正**：该结论不成立；"没检测到"是 harness 缺陷，不是"没有 hang"。

## E-009 · 2026-09-27 · **ROOT CAUSE：native stack 已取**

### 9.1 抓栈前提：harness 曾经是"盲"的（**harness 缺陷，非产品 hang**）

长批次日志写 `findings=0`，但批结束时系统里**同时存在 37 个 `nhi1_native_probe` 进程**
（各 1 线程、~40MB、启动时间散布 4 小时），而 harness 把这些 run 全记成 `exited rc=0`。

- 根因：改用 `Start-Process -PassThru` 后，PS 5.1 返回对象的 **`HasExited` 不可信**；
  早先用 .NET `Process` API 的版本**能**抓到（E-007 见证即由它抓到）。
- **判定：这是 harness 缺陷，不能与产品 hang 混为一谈。**
- **仓库约定 3** 由此固化：进程存活判定**不得**依赖 `Start-Process -PassThru`。

### 9.2 抓到的 native 栈（cdb `-pv` 非侵入式 attach，进程已停滞 4 小时）

```text
ntdll!RtlUserThreadStart
nhi1_native_probe!mainCRTStartup
nhi1_native_probe!__scrt_common_main_seh
ucrtbased!exit
ucrtbased!wassert
KERNEL32!ExitProcess
ntdll!RtlExitUserProcess
ntdll!LdrShutdownProcess                 ← 进程已进入 loader shutdown
ntdll!RtlProcessFlsData                  ← 执行 FLS 回调
nvspcap64!QueryShadowPlayDdiShimInterface+0xc0334   ← NVIDIA ShadowPlay overlay 的 FLS 回调
nvspcap64!QueryShadowPlayDdiShimInterface+0xbc9fa
clang_rt_asan_dynamic_x86_64!_asan_wrap_RtlFreeHeap        ← 该回调里调用 free()
clang_rt_asan_dynamic_x86_64!_asan_wrap__CrtIsValidHeapPointer
clang_rt_asan_dynamic_x86_64!_asan_wrap__CrtIsValidHeapPointer
clang_rt_asan_dynamic_x86_64!_sanitizer_internal_memset
clang_rt_asan_dynamic_x86_64!_sanitizer_get_dtls_size__dll
KERNELBASE!WaitOnAddress
ntdll!RtlWaitOnAddress
ntdll!ZwWaitForAlertByThreadId           ← 永久自旋，进程永不完成退出
```

`lm` 显示同时加载：`nvspcap64.dll`（NVIDIA ShadowPlay）、`clang_rt_asan_dynamic-x86_64.dll`、
`nvoglv64`、`EditorBridge`、`OpenAL32`。

### 9.3 根因判定

**不是 EngineHost / GLFW / OpenGL / teardown 的缺陷。**
`main` 返回后，`LdrShutdownProcess` 触发 FLS 回调；其中 **NVIDIA ShadowPlay overlay
（`nvspcap64.dll`）的 FLS 回调调用 `free()`**，该 `free()` 被 **MSVC ASan 分配器拦截**接管，
进入 ASan TLS/分配器初始化后在 `WaitOnAddress` 上自旋死等 → 进程永远无法完成退出。

这解释了此前全部反直觉现象：hang 严格发生在最后一行之后、退出耗时渐进增长后挂死、
复现率依赖负载、CLR 已卸载导致 `dotnet-stack` 无法 attach、Kill 后迟迟不消失、偶发 `0xC0000374`。

## E-010 · 2026-09-28 · Closure B 受控对比：ASan vs 非 ASan

同一 probe、同一异常拆除路径，只替换被测 `EditorBridge.dll`：

| 被测桥接 DLL | 次数 | 挂起 | 退出阶段 avg | max |
|---|---|---|---|---|
| **Debug（ASan 插桩）** | 12,225 | **37（≈0.30%，OS 级可数）** | 600–900ms | 1.8s |
| **Release（非 ASan）** | 400 | **0** | **78ms** | 178ms |

补充事实：

- Release 批次结束后系统中**无任何残留** `nhi1_native_probe`；Debug 批次同期 37 个（各 1 线程 / ~40MB，存活 4 小时不退出）。
- 退出阶段耗时相差约 **15 倍**，且 Debug 侧存在渐进增长的长尾。
- Release 批次沿用了同一个"盲"检测器，故"0 findings"本身不是铁证；
  **铁证是残留进程计数（OS 级、不依赖 harness）与退出阶段耗时分布**。

**必要条件**：① 进程被全局注入 `nvspcap64.dll`；② 加载 ASan 插桩的 `EditorBridge.dll`。
去掉 ASan 后，相同异常拆除路径不再出现退出期停滞。

## E-011 · 2026-09-28 · **CLOSURE B（裁决落地）**

### 11.1 有效证据台账（唯一采信的一组）

```text
OS 级残留进程（Debug ASan 批次同期）        37
真实挂起率                                  37 / 12,225 ≈ 0.30%
Release（非 ASan）                          0 / 400
native stack                                直接定位到 NVIDIA overlay FLS 回调 + ASan 分配器死锁
收口时 residual processes                   0
被作废的记录                                findings=0(12,225) · 80 次无命中 · 60 次无命中
作废原因                                    harness 用 Start-Process -PassThru 的 HasExited 判定进程存活（PS 5.1 不可信）
```

### 11.2 处置（裁决：① + ③；② 作为证据分层策略保留）

| # | 处置 | 内容 |
|---|---|---|
| ① | **runner 排除 `nvspcap64.dll` 注入**（首选环境修正） | 在跑 ASan 测试的机器/runner 上禁用 NVIDIA ShadowPlay / GeForce overlay 注入；验证方式：被测进程的 `lm`/模块列表中不得出现 `nvspcap64.dll` |
| ③ | **登记为测试基础设施约束**（不得省略） | 永久记录：本机全局 NVIDIA overlay 注入 + MSVC ASan 桥接 DLL 组合会在**进程退出 FLS 回调**阶段产生第三方死锁；**不归因于** EngineHost / GLFW / GL teardown |
| ② | **证据分层策略（保留，不替代）** | Debug + ASan = 功能 / 内存安全 / 生命周期负例的**主要**证据；Release / non-ASan = **进程退出稳定性**这一维度的**独立**证据；Release 结果**不能**替代 ASan 的功能与内存检测价值 |

### 11.3 三条仓库约定（固化）

1. 含 CJK 的 `.ps1` **必须带 UTF-8 BOM**（PS 5.1 否则按 ANSI 解码致解析错位）。
2. native C++ 若 include 无 BOM 的 UTF-8 头（如 `capi.h`），**必须 `/utf-8`**
   （与主工程 `CMakeLists.txt:39` 一致）。
3. **进程存活判定不得依赖 `Start-Process -PassThru` 的 `HasExited`**；改用 .NET `Process` API，
   并以 OS 级进程计数交叉验证。

### 11.4 明确声明（防止后续误读）

- **这不是产品修复**，也没有做过任何"禁 overlay 后跑 N 次即宣布修复"的动作。
- 正确结论是：**根因已识别 + 受控对比完成 + 第三方基础设施条件已隔离**。
- **native / teardown 产品代码零修改**；A2 的三个真实缺陷
  （`TargetInvocationException` 逃逸 / pump failure 被丢弃 / `Shutdown` 未完成仍 `Destroy`）
  保持已修状态，且**均非本次 hang 的成因**。
- A8 继续 DEFERRED。

### 11.5 状态变更

```text
NHI-1   CLOSED / Closure B
A2      IMPLEMENTED / 7-7 GREEN；NATIVE TEARDOWN FINDING → CLOSED
HRC-2-A CONDITIONAL PASS（F-1 已清、F-2 已实现）
next    Stage A / A3
```
