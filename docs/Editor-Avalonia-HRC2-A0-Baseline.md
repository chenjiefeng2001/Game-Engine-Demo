# Editor Avalonia HRC-2-A0 — Baseline Lock

> **阶段**：UI-HRC-2-A / A0  
> **日期**：2026-09-24  
> **状态**：`COMPLETE`  
> **目的**：锁定 HRC-2-A 开始前的代码、ABI、构建和 HRC-1 回归基线  
> **代码变更**：无  
> **提交/tag**：无

---

## 1. Repository identity

| 项目 | 值 |
|---|---|
| Branch | `avalonia` |
| HEAD | `d3ba5902fb7473dabf8435f786450c922e5f72b1` |
| Describe | `engine-foundation-v1-f2-b-collider-dirty` |
| tracked diff fingerprint | `0254154b1d8b78dccc0a5f3646cc5c43841f3d91` |
| index | clean |
| canonical Application source | `engine/src/Core/Application.cpp` |

fingerprint 由 `git diff --binary | git hash-object --stdin` 计算，不包含未跟踪文件。

---

## 2. Dirty baseline

A0 开始前已存在 Phase 7/HRC-1 tracked changes，主要重叠文件包括：

```text
editor_avalonia/Program.cs
editor_avalonia/App.axaml.cs
editor_avalonia/MainWindow.axaml.cs
editor_avalonia/Services/EditorHostService.cs
editor_avalonia/ViewModels/MainViewModel.cs
bridge/include/editor_bridge/capi.h
bridge/src/EditorSession.cpp
bridge/src/EditorSession.h
bridge/src/capi.cpp
engine/include/Engine/Application.h
engine/src/Core/Application.cpp
```

既有未跟踪内容包括 HRC 文档、Phase 7 spike、`Hrc1Gate.cs` 和 `EditorCameraInput.h`。A0 不清理、不 reset、不 stash、不提交这些内容。

A0 复跑 Gate 后再次计算 fingerprint，仍为：

```text
0254154b1d8b78dccc0a5f3646cc5c43841f3d91
```

---

## 3. Managed build

命令：

```text
dotnet build editor_avalonia/AvaloniaEditor.csproj -c Debug --no-restore
```

结果：

```text
Build succeeded.
0 Warning(s)
0 Error(s)
```

本次增量 build 没有产生 warning；历史 Closure 中的 warning 记录仍保留，不视为本轮新增回归。

---

## 4. Native bridge build and test

命令：

```text
cmake --build build --config Debug --target EditorBridge test_bridge
ctest --test-dir build -C Debug -R "^test_bridge$" --output-on-failure
```

结果：

```text
EditorBridge: PASS
test_bridge: PASS
1/1 tests passed
```

`test_ecs` 未运行。它是 `docs/Evidence-Baseline.md` 中登记的独立 EXCLUDED baseline，不属于 HRC-1 回归集合。

---

## 5. HRC-1 and existing UI gates

执行方式：串行启动 `editor_avalonia/bin/Debug/net9.0/AvaloniaEditor.exe`，每次等待进程退出。

| Gate | Exit | 结果 |
|---|---:|---|
| `--hrc1` | 0 | `HRC-G1 GATE ALL GREEN` |
| `--selftest` | 0 | `PHASE0 GATE ALL GREEN` |
| `--gate1` | 0 | `PHASE1 GATE ALL GREEN` |
| `--gate2` | 0 | `PHASE2 GATE ALL GREEN` |
| `--gate3a` | 0 | `P3-A GATE ALL GREEN` |
| `--gate3b` | 0 | `P3-B GATE ALL GREEN` |
| `--gate3c` | 0 | `P3-C GATE ALL GREEN` |
| `--gate3d` | 0 | `P3-D GATE ALL GREEN` |
| `--gate3g` | 0 | `AV-G3 GATE ALL GREEN` |

所有 HRC-1/Phase regression 均为绿色。

运行过程中出现的 `interception_win: unhandled instruction` 文本未导致进程失败；所有 gate exit code 为 0。该输出不作为 HRC-2-A 通过证据，后续需在 HRC-2 native Gate 中单独记录 sanitizer/异常边界。

---

## 6. A0 acceptance

- [x] HEAD、branch、describe 已记录。
- [x] tracked diff fingerprint 已记录并在 Gate 后复核。
- [x] canonical Application source 已固定为大写 `engine/src/Core`。
- [x] managed build 通过。
- [x] `EditorBridge` build 通过。
- [x] `test_bridge` 通过。
- [x] HRC-1 和既有 UI Gate 全部通过。
- [x] 未执行 reset/clean/stash。
- [x] 未创建 commit/tag。
- [x] 未修改实现代码。

---

## 7. HRC-2-A decision after A0

```text
A0 baseline lock:       COMPLETE
HRC-2-P core topology:  FROZEN
HRC-2-A:                READY TO START A1
HRC-G2P-01~08:          NOT GREEN
```

A1 只新增 EngineHost ABI skeleton、`RuntimeResult` layout 和 exception-safe wrapper；不创建真实 GL context、不替换 production Host、不宣称 HRC-2-A runtime 已实现。
