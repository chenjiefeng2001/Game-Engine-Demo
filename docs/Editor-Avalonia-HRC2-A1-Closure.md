# Editor Avalonia HRC-2-A1 — ABI Skeleton Closure

> **阶段**：UI-HRC-2-A / A1  
> **日期**：2026-09-24  
> **状态**：`COMPLETE`  
> **前置**：`docs/Editor-Avalonia-HRC2-A0-Baseline.md`  
> **提交/tag**：无

---

## 1. Scope completed

A1 只建立新 runtime boundary，不实现真实 GL、Engine loop、Session binding 或 production Host 切换：

1. 新增 opaque `EngineHostHandle`。
2. 新增独立 `EngineHost_GetApiVersion`，不改变旧 `EditorBridge_GetVersion` v1.1。
3. 新增 1-byte packed `RuntimeResult`。
4. 新增 typed status/stage/state constants。
5. 新增 `EngineHost_*` C ABI skeleton。
6. 新增 C# P/Invoke mirror。
7. 新增 C++ exception guard 和 null-handle contract。
8. 新增 ABI layout/lifecycle/null-handle tests。

旧 `EditorSession_*` ABI、版本号和调用路径保持不变。

---

## 2. Files changed by A1

```text
bridge/CMakeLists.txt
bridge/include/editor_bridge/capi.h
bridge/src/capi.cpp
bridge/src/EngineHost.h
bridge/src/EngineHost.cpp
editor_avalonia/Native/EditorBridgeApi.cs
tests/test_bridge/test_editor_capi.cpp
```

这些文件与 HRC-1/Phase 7 dirty baseline 有部分重叠；没有清理或覆盖既有改动。

---

## 3. ABI contract

### 3.1 Version

```text
EditorBridge_GetVersion()  = v1.1
EngineHost_GetApiVersion() = v1.0
```

### 3.2 RuntimeResult layout

Native struct 使用 `#pragma pack(push, 1)`；C# 使用 `LayoutKind.Sequential, Pack = 1`。

| Field | Offset | Type |
|---|---:|---|
| `struct_size` | 0 | `uint32_t` |
| `abi_version` | 4 | `uint32_t` |
| `status` | 8 | `int32_t` |
| `stage` | 12 | `int32_t` |
| `operation_id` | 16 | `uint64_t` |
| `frame_id` | 24 | `uint64_t` |
| `generation` | 32 | `uint64_t` |
| `owner_thread_id` | 40 | `uint64_t` |
| `state_before` | 48 | `int32_t` |
| `state_after` | 52 | `int32_t` |
| `error_code` | 56 | `int32_t` |
| `diagnostic_length` | 60 | `uint32_t` |
| `diagnostic_truncated` | 64 | `uint32_t` |
| `raw_dt_seconds` | 68 | `float` |
| `effective_dt_seconds` | 72 | `float` |
| total size | 76 | bytes |

### 3.3 New exports

```text
EngineHost_GetApiVersion
EngineHost_Create
EngineHost_OpenProject
EngineHost_Play
EngineHost_Reload
EngineHost_Stop
EngineHost_PumpOneFrame
EngineHost_CloseProject
EngineHost_Shutdown
EngineHost_Destroy
EngineHost_GetLastFrameResult
EngineHost_SetEventCallback
EngineHost_GetLastDiagnostic
```

A1 中未实现的项目返回 typed `InvalidState`；这不是 runtime-ready 证据。

---

## 4. Verification

### Native build/test

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

新增测试覆盖：

- `RuntimeResult` size/offset；
- `EngineHost_Create` bootstrap result；
- typed unimplemented operation；
- shutdown result；
- repeated shutdown；
- null result/handle；
- null destroy。

### Managed build

```text
dotnet build editor_avalonia/AvaloniaEditor.csproj -c Debug --no-restore
```

结果：

```text
Build succeeded
0 errors
```

首次 A1 增量 build 记录了既有 4 个 managed warnings；后续增量 build 为 0 warnings。A1 没有新增 warning。

### HRC-1 regression

以下入口全部 exit 0：

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

所有既有 Gate 输出 `ALL GREEN`。

---

## 5. Dirty baseline after A1

| 项目 | 值 |
|---|---|
| HEAD | `d3ba5902fb7473dabf8435f786450c922e5f72b1` |
| A0 fingerprint | `0254154b1d8b78dccc0a5f3646cc5c43841f3d91` |
| A1 tracked diff fingerprint | `a4eebba28a6066358fa65b74ad3d0172e1aa7d61` |
| index | clean |
| commit/tag | 未创建 |

A1 fingerprint 包含既有 tracked dirty changes 和 A1 tracked changes，不包含未跟踪 HRC 文档、`EngineHost.*` 源文件或 spike 文件。A0 fingerprint 仍作为可回溯基线保留。

---

## 6. A1 boundary

A1 **没有**：

- 创建 runtime thread；
- 创建 Hidden GLFW persistent context；
- 创建 Host-owned FBO；
- 接入 `Application::PumpOneFrame`；
- 迁移 `EditorHostService`；
- 替换旧 `EditorSession_*` production path；
- 证明 HRC-G2P-01～08 GREEN。

A1 **已经**证明：

- 新 ABI 可编译、导出、链接；
- typed result 的 native/C# layout 可镜像；
- null/error/重复 shutdown contract 有测试；
- 旧 ABI 和 HRC-1 regression 不受影响。

---

## 7. Decision

```text
A0 baseline lock:       COMPLETE
A1 ABI skeleton:        COMPLETE
A2 thread/queue:        READY TO START
HRC-G2P-01~08:          NOT GREEN
HRC-2-A:                IN PROGRESS
```

下一步是 A2：只实现一个 native `std::jthread`、command/completion queue、manual pump、owner-thread metadata、stop/join 和 typed `WrongThread`/`InvalidState`；仍不创建真实 GL context。
