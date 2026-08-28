# Avalonia Phase 3 — 会话卡死 ASan 诊断报告

> 日期：2026-08-28 · 分支：avalonia · 依据：`docs/Avalonia-Phase3-Charter.md`
> 触发：上个 session 直跑 `test_bridge.exe` 时**直接卡死**（无响应、无法退出，
> 需强制结束 + rename 绕过 exe/pdb 文件锁 → `.zomb` 遗留）。
> 本报告以 ASan 复现 + 进程证据 + 日志时间线给出根因与处置。

---

## 0. 结论速览

| # | 根因 | 判定 | 证据 |
|---|------|------|------|
| FZ-1 | **ASan interception 失败 → CHECK 硬失败**：MSVC ASan 的 Windows interception 层在 `interception_win.cpp:193` 遇到无法解码的指令序列，缺省（未设 `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1`）时**直接卡死/退出** | **确证**（§3 复现） | `interception_win: unhandled instruction at 0x7ffa81d82430: 80 3a 00 4c 8b d1 75 04` 出现在**每个** ASan 进程 stderr；无 env 时 CHECK failed 立即出现 |
| FZ-2 | **多会话同进程 GL 重建是正常路径，非根因**：test_bridge 每用例 `EditorSession_Create/Destroy` → 引擎进程内重建 GL；带 env 时全绿 | **排除** | 根 `logs/engine.log` 10:54:40 窗口 3 次 `OpenGL 4.6 loaded`，对应用例逐条，全部 PASS |
| FZ-3 | ~~gate3b reopen 二次会话卡死~~ → **实际 gate3b 通过**（10:57 exit=0）；engine.log 截断 = 异步 logger 未 flush 即 `Environment.Exit` | **排除**（纠正） | selftest.log:222-223 `[P3-B GATE] ALL GREEN` + `gate3b exit=0` |
| FZ-4 | **环境僵尸进程残留**：此前 session 的 `test_content.exe` 僵尸（ASan interception 死锁态）锁死 exe/pdb | **已登记**（既有已知项） | `build/tests/Debug/test_content.exe.zomb`（08-24/08-25）+ 本次 `test_bridge.exe.zomb`（10:42） |

**结论**：卡死根因 = **MSVC ASan 在 Windows 上的 interception 层缺陷**。
bridge/engine 业务代码 ASan 全程干净（无 UAF/越界/泄漏）。
修法 = 运行环境固化（env 变量）+ 构建/工具链防呆（§4 已落地）：
**AvaloniaEditor 在 `Program.cs` 首行自设 env**（早于首次 P/Invoke）；
**C++ 测试 exe 无程序内自设位置**（env 必须在 CRT/ASan 初始化前生效，
main() 内设置已太晚），由 CTest 环境 + `tools/run_test.cmd` 统一兜底。

---

## 1. 触发场景与时间线（2026-08-28）

| 时间 | 事件 | 结果 |
|------|------|------|
| 09:56 | integrity gate build 阶段（build_test_*.log） | 编译通过 |
| 10:22:35 | `--gate3a` 首跑 | FAIL（selection restore 断言，已修） |
| 10:24:16 | `--gate3a` 复跑 | ALL GREEN |
| 10:25 | integrity gate I3（i3_*.log） | DF01–DF09 + GP01 全 PASS |
| **10:42:08** | **`test_bridge.exe` 直跑（无 ASan env）** | **卡死** → `.zomb` rename 绕过（exe+pdb） |
| 10:54:40 | test_bridge 复跑（带 env） | PASS（根 engine.log 3 次 GL init 全绿） |
| 10:57:06 | `--gate3b`（P3-B Inspector） | **ALL GREEN exit=0**（selftest.log 实证） |
| 11:02:06 | `--gate2` | ALL GREEN exit=0 |

关键事实：**所有** ASan 进程的 stderr 都带同一条
`interception_win: unhandled instruction at 0x7ffa81d82430`。
这条消息本身无害（有 env 时进程继续），**无 env 时 ASan 直接 CHECK 失败**（§3）。

---

## 2. 证据文件清单

| 文件 | 内容 | 作用 |
|------|------|------|
| `build/tests/Debug/test_bridge.exe.zomb` + `.pdb.zomb`（10:42:08） | 卡死进程 rename 遗留 | **FZ-1 直接现场**（时间点精确） |
| `build/tests/Debug/test_content.exe.zomb` / `.zombie_locked.*`（08-24/25） | 僵尸进程（既有同类） | FZ-4 |
| `logs/editor_stderr.log` / `logs/gate/probe_r.log.err` | `interception_win: unhandled instruction` ×2 | FZ-1 现场 |
| `logs/gate/i3_DF*.log.err` | 同上（每个 I3 进程都有，但 gate 带 env → PASS） | FZ-1 普遍性 + 对照 |
| `build/Testing/Temporary/LastTest.log` | 同上 ×10 | FZ-1 普遍性 |
| `logs/engine.log`（10:54:40 窗） | `OpenGL 4.6 loaded` ×3 / 用例逐条 | FZ-2 排除对照 |
| `editor_avalonia/selftest.log:222-223` | `[P3-B GATE] ALL GREEN` + `gate3b exit=0` | **FZ-3 排除**（gate3b 实际通过） |
| `editor_avalonia/Program.cs:12-13` | 首行自设 ASAN env | 编辑器 gate 安全的原因 |

---

## 3. 复现与根因确认（本 session 实测）

### 3.1 复现命令

```bash
# A. 无 ASan env —— 复现 CHECK 硬失败（卡死/退出模式）
./build/tests/Debug/test_bridge.exe --gtest_filter=EditorBridgeTest.ScriptBindingContract
# → stderr:
#   interception_win: unhandled instruction at 0x7ffa81d82430: 80 3a 00 4c 8b d1 75 04
#   AddressSanitizer: CHECK failed: interception_win.cpp:193
#   "Interception failure, stopping early. Set ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1 to try to continue."

# B. 设置 env —— 正常通过（integrity gate 同款）
ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1 ./build/tests/Debug/test_bridge.exe
# → 16/16 PASS（含 P3-B/P3-C 契约），无任何 ASan 报告
```

### 3.2 根因分析

`0x7ffa81d82430` 处的字节 `80 3a 00 4c 8b d1 75 04` 位于进程启动早期加载的
某个模块内（interception 在 CRT/依赖 DLL 上做 inline hook 时遇到无法解码的
指令前缀）。这是 MSVC ASan interception 层在 Windows x64 上的已知缺陷类：
对某些指令序列（此处是 `80 3a 00` = `cmp byte ptr [rdx],0` 家族）无法识别，
缺省行为是**报 CHECK 失败并停止** —— 在无控制台交互的 gate 场景表现为
进程挂起（卡死），有对话框/调试器时表现为等待外部关闭（同样卡死）。

integrity gate 早已知道此坑（`tools/integrity_gate.ps1:37` 显式设置 env），
`docs/GP1-D-Human-Run.md:10` 也登记过；但 **C++ 测试 exe 直跑没有内置保护**
（不像 AvaloniaEditor 在 Program.cs 首行自设），故手工直跑复现卡死。

### 3.3 为什么 gate3b 没卡（FZ-3 纠正）

`editor_avalonia/Program.cs` 在 `Main` 首行、**早于首次 P/Invoke（DLL 加载）
之前**执行 `Environment.SetEnvironmentVariable("ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE", "1")`
—— 这正是 GP1-DX 教训（audit §8.2）要求的时序。因此所有 `--gate*` 直跑
进程在 ASan interception 触发前 env 已是 "1"，进程继续，gate 全绿。
本 session 实测 gate1/2/3a/3b 全部 ALL GREEN 佐证。

---

## 4. 处置（2026-08-28 已落地，不再是"仅登记"）

C++ 侧没有 Program.cs 式的"首行自设"位置（env 必须在进程启动早期、
CRT/ASan 初始化前生效，`main()` 内设置太晚）。因此防护落在构建/工具链层：

### 4.1 CTest 环境固化（`tests/CMakeLists.txt`）

`ENABLE_ASAN AND MSVC` 时对**全部**测试 target 设 `ENVIRONMENT`
`ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1` → `ctest` 直跑永不卡死。
同时统一 `WORKING_DIRECTORY = 仓库根`（修复 ctest 缺省在 build/tests 下
运行时 `assets/gp01/...` 相对路径全部落空的问题，与 integrity_gate 语义一致）。

### 4.2 直跑防护（`tools/run_test.cmd`）

手跑 ASan 测试 exe 一律走启动器前置设置 env（main() 内设置已太晚）：

```
run_test.cmd build\tests\Debug\test_bridge.exe --gtest_filter=EditorBridgeTest.*
```

AvaloniaEditor 侧维持 `Program.cs` 首行自设 + `Phase3Gate` 顶部 WARN 防呆。

### 4.3 僵尸进程处置（本 session 实测）

- 10:42 冻结的 `test_bridge.exe`（PID 55404）已确认退出（`HasExited=True`）
  并已清理；其 CWD 锁 `gp01_editor_scratch/gate_p2/logs/engine.log` 已释放
  （此前导致 test_gp01 `remove_all` 失败）。
- `.zomb` 遗留保留为证据（build/ 已在 .gitignore）。
- 例行 `Get-Process test_*` 检查僵尸（audit §8 建议）。

---

## 5. 验证记录（本 session + 处置落地后复验）

| 项 | 结果 |
|----|------|
| test_bridge 无 env（复现） | `interception_win: unhandled instruction` → CHECK failed: interception_win.cpp:193（卡死模式） |
| test_bridge 带 env | **16/16 PASS**（含 P3-B/P3-C 契约） |
| ctest test_bridge（shell 无 env，靠 CTest 属性） | **PASS**（防呆生效） |
| 僵尸进程 | PID 55404 已退出并清理，`gp01_editor_scratch` 锁释放 |
| 全量 ctest（9/10） | test_bridge/test_gp01/test_content 等 **全绿**；仅 test_ecs EXCLUDED（旧 API 无法编译，另案） |
| test_core | exit=3 首测崩溃修复（vec3 未初始化读 → 值初始化），20/20 PASS，基线已纳入 |
| ASan 内存报告 | 无 UAF / 越界 / 泄漏（全程） |

**代码改动落地**：`tests/CMakeLists.txt`（CTest env + CWD）、
`tools/run_test.cmd`（直跑启动器）、`tests/test_core/math/Vector3Test.cpp`
（vec3 值初始化）、`docs/Evidence-Baseline.md`（test_core 纳入）。
根因仍在 ASan 运行时环境，不在 bridge/engine 业务代码。

---

## 6. 结论

1. 卡死根因 = **MSVC ASan Windows interception 缺陷**（`interception_win.cpp:193`）；
2. **已落地**：CTest env 固化 + `tools/run_test.cmd` 启动器 + AvaloniaEditor
   Program.cs 首行自设 → 直跑/ctest/gate 均不再卡死（本 session 全量复验绿）；
3. 僵尸进程（PID 55404）已清理，`gp01_editor_scratch` 锁释放；
4. 附带修复：test_core exit=3 首测崩溃（vec3 未初始化读）→ 基线纳入，
   全量仅剩 test_ecs EXCLUDED（旧 API 无法编译，另案）；
5. 业务代码 ASan 全程干净，Phase 3 继续推进无内存层阻塞。
