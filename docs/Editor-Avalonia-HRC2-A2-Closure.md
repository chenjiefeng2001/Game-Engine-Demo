# Editor Avalonia HRC-2-A2 — Runtime Thread and Queue Closure

> **阶段**：UI-HRC-2-A / A2  
> **日期**：2026-09-24  
> **状态**：`COMPLETE`  
> **前置**：`docs/Editor-Avalonia-HRC2-A1-Closure.md`  
> **提交/tag**：无

---

## 1. Scope completed

A2 建立单一 native runtime owner：

1. 一个 `std::jthread` runtime thread；
2. command queue 和同步 completion；
3. runtime owner thread ID；
4. manual `PumpOneFrame`；
5. monotonic-independent raw/effective dt result（调用者提供 raw dt，effective clamp `0.1s`）；
6. invalid/negative/non-finite dt rejection；
7. `WrongThread` owner-thread shutdown rejection；
8. stop/join/shutdown acknowledgement；
9. Faulted sticky state；
10. emergency owner-thread teardown seam；
11. stage-specific exception status；
12. bounded-command contract。

A2 仍不创建 GL context、Application 或 FBO；所有 `OpenProject/Play/Reload/CloseProject` 仍明确返回未实现状态。

---

## 2. Files changed by A2

```text
bridge/CMakeLists.txt
bridge/include/editor_bridge/capi.h
bridge/src/capi.cpp
bridge/src/EngineHost.h
bridge/src/EngineHost.cpp
editor_avalonia/Native/EditorBridgeApi.cs
tests/test_bridge/test_editor_capi.cpp
```

A2 与 A1 使用同一组 ABI 文件；旧 `EditorSession_*` 实现未重写。

---

## 3. Runtime contract

```text
caller thread
  → validate handle/result
  → enqueue command
  → wait completion
  → receive typed RuntimeResult

runtime thread
  → execute exactly one command
  → publish result
  → continue waiting
```

当前单帧行为：

```text
non-finite/negative dt → TickFault, no frame increment
valid dt              → frame_id++, SkippedNotPlaying
raw dt                → result.raw_dt_seconds
effective dt          → min(raw dt, 0.1)
```

runtime command contract 是同步且有界的；A3 引入 GL/Engine 操作时，必须保持每个 command 可停止、可回滚，不得在 runtime thread 执行无界阻塞操作。

---

## 4. Shutdown and failure contract

正常 shutdown：

```text
reject new commands
  → enqueue shutdown command
  → owner thread marks Stopped
  → ShutdownComplete result
  → stop/join runtime thread
```

异常/失败：

```text
runtime exception or enqueue failure
  → Faulted state
  → emergency teardown request
  → owner thread teardown seam
  → stop/join
  → ShutdownFailed result
```

约束：

- `Faulted` 不得被后续 Shutdown 洗成 `ShutdownComplete`；
- runtime owner thread 不得同步等待自己；
- shutdown result 必须区分 `ShutdownComplete` 与 `ShutdownFailed`；
- A3 必须实现 `PerformEmergencyTeardown()` 对 GL/Session/FBO 的实际 owner-thread teardown；
- A2 外部验证使用每个 Gate 60 秒硬超时，超时进程由 runner kill，不允许无界等待。

---

## 5. Verification

### Native

```text
cmake --build build --config Debug --target EditorBridge test_bridge
ctest --test-dir build -C Debug -R "^test_bridge$" --output-on-failure
```

结果：

```text
EditorBridge: PASS
test_bridge: PASS
100% tests passed
```

A2 tests 覆盖：

- owner thread ID 在多帧间稳定；
- command queue 从非 runtime caller 执行；
- `PumpOneFrame` frame id；
- raw/effective dt；
- dt fault；
- null result 无副作用；
- diagnostic 两调用和 `cap=0` canary；
- repeated shutdown；
- shutdown 后 Stop 幂等；
- last frame result 不被 Shutdown 覆盖；
- layout/null contract。

### Managed

```text
dotnet build editor_avalonia/AvaloniaEditor.csproj -c Debug --no-restore
```

结果：

```text
Build succeeded
0 Warning(s)
0 Error(s)
```

### HRC-1 regression

使用每个 Gate 60 秒硬超时的串行 runner，以下全部 exit 0：

```text
--hrc1
--selftest
--gate1
--gate2
--gate3a
--gate3b
--gate3c
--gate3d
--gate3g
```

一次早期无界串行验证在 gate3b 后被外部工具中止；随后 `--gate3c`、`--gate3d`、`--gate3g` 均以硬超时 runner 单独通过，最终全量 bounded runner 也全部通过。没有残留进程。

---

## 6. Dirty baseline after A2

| 项目 | 值 |
|---|---|
| HEAD | `d3ba5902fb7473dabf8435f786450c922e5f72b1` |
| A0 fingerprint | `0254154b1d8b78dccc0a5f3646cc5c43841f3d91` |
| A1 fingerprint | `a4eebba28a6066358fa65b74ad3d0172e1aa7d61` |
| A2 tracked diff fingerprint | `bd7a385ead69023927f8a6f4583a71669d65cd5b` |
| index | clean |
| commit/tag | 未创建 |

fingerprint 不包含未跟踪的 `EngineHost.*`、HRC 文档和 spike 文件。

---

## 7. A2 boundary

A2 **没有**：

- persistent Hidden GLFW context；
- GLAD readiness；
- `Application::Initialize/PumpOneFrame/Shutdown`；
- Host-owned FBO；
- runtime-bound `EditorSession`；
- production `EditorHostService` 切换；
- failure injection matrix；
- HRC-G2P-01~08 GREEN。

A2 **已经**证明：

- single runtime thread/queue owner 成立；
- typed result、dt、frame id、owner metadata 可查询；
- shutdown/join 不再在当前 bounded command 路径卡死；
- Faulted/exception/null contracts 有明确行为；
- 外部验证具备硬超时和 kill 退出信号；
- HRC-1 regression 保持绿色。

---

## 8. Decision

```text
A0 baseline lock:       COMPLETE
A1 ABI skeleton:        COMPLETE
A2 thread/queue:        COMPLETE
A3 Hidden GLFW:         READY TO START
HRC-G2P-01~08:          NOT GREEN
HRC-2-A:                IN PROGRESS
```

下一步是 A3：在 runtime thread 内接入 persistent Hidden GLFW context、GLAD readiness、make/un-current 协议和 `glfwTerminate` exactly-once owner；不得把 context 创建放回 UI/caller thread。
