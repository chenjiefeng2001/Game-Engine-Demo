# Editor Avalonia HRC-1 Closure — Single Host Ownership

> **阶段**：UI-HRC-1 Closure / Freeze Gate  
> **日期**：2026-09-24  
> **实现状态**：`PASS`  
> **Ownership Gate**：`HRC-G1 ALL GREEN`  
> **Immutable commit/tag**：`NOT CREATED — blocked by pre-existing dirty Phase 7 baseline`  
> **UI status**：`PARTIAL / NOT FROZEN`

---

## 1. Closure decision

HRC-1 的实现与 HRC-G1 证据已经完成。当前实现满足本阶段冻结的 ownership 边界：

```text
Program
  └─ one EditorHostService
       └─ App
            └─ MainWindow
                 └─ MainViewModel
```

本阶段验证的是：

- Host ownership；
- App → Window → ViewModel 的注入关系；
- `CloseProject` 与 terminal `Dispose` 的生命周期分离；
- UI 不再独立创建第二个 Host；
- terminal teardown 后不能重新创建 session。

本阶段**不验证**：

- Engine bootstrap；
- 持久 GL context/surface；
- RuntimeTick production loop；
- Viewport render/readback/resize；
- Picking；
- Gizmo；
- ECS；
- ImGui migration。

因此 HRC-1 Closure 的结论是：

```text
HRC-1 implementation: PASS
HRC-G1:               PASS
HRC-1 immutable tag:  BLOCKED / NOT CREATED
Avalonia UI:          PARTIAL / NOT FROZEN
HRC-2:                NOT STARTED
```

---

## 2. Baseline and provenance

### 2.1 HRC-0 baseline

| 项目 | 值 |
|---|---|
| HRC-0 document | `docs/Editor-Avalonia-HRC0-Audit.md` |
| Repository HEAD | `d3ba5902fb7473dabf8435f786450c922e5f72b1` |
| Branch | `avalonia` |
| `git describe` | `engine-foundation-v1-f2-b-collider-dirty` |
| HRC-0 conclusion | UI `PARTIAL / NOT FROZEN` |
| HRC-0 source state | Phase 7/Bridge/Editor 改动已在工作树中，尚未形成 clean commit |

HRC-1 是在 HRC-0 记录的 dirty working tree 上完成的，不应把 HRC-0 HEAD 当作 HRC-1 的完整实现 SHA。

### 2.2 Current working-tree fingerprint

为避免后续无法区分工作树漂移，当前状态记录如下：

```text
git diff --binary | git hash-object --stdin
0254154b1d8b78dccc0a5f3646cc5c43841f3d91

git diff --cached --binary | git hash-object --stdin
e69de29bb2d1d6434b8b29ae775ad8c2e48c5391
```

第一条是 tracked worktree diff 指纹，包含 HRC-0 前置 Phase 7 改动与 HRC-1 对 tracked 文件的改动；第二条表示 Git index 当前为空。未跟踪的 HRC-1 文件不包含在该 hash 中，已在 6.2 单独列出。

### 2.3 HRC-1 commit SHA

```text
HRC-1 commit SHA: NOT CREATED
```

原因不是 Gate 失败，而是当前 dirty baseline 无法安全生成“只含 HRC-1”的 commit：

- HRC-1 修改了 `MainWindow.axaml.cs`、`MainViewModel.cs`、`EditorHostService.cs`；
- 这些文件在 HRC-1 之前已经包含未提交 Phase 7 改动；
- 同一文件中的 HRC-1 hunk 与 Phase 7 hunk 交错；
- 仅暂存 HRC-1 hunk 会产生缺少 Phase 7 依赖的不可复现 commit，或误把 Phase 7 一并纳入。

在未获得 clean baseline 或明确授权前，不执行 reset、stash、清理或提交。

### 2.4 Tag decision

未创建建议的 tag：

```text
engine-editor-avalonia-hrc1-host
```

原因：当前没有可安全指向“仅 HRC-1”的 immutable commit。创建 tag 会把混合 dirty state 误标为 HRC-1 baseline，违反 HRC-0 的可复现性纪律。

---

## 3. HRC-1 implementation surface

### 3.1 Composition root

`Program` 现在是唯一 composition root，在启动 Avalonia 前创建一个 Host，并由 `using` 负责 terminal teardown：

- `editor_avalonia/Program.cs:15-22`

### 3.2 App injection

`App` 接收 Host，并通过构造函数注入 `MainWindow`：

- `editor_avalonia/App.axaml.cs:11-25`

App 不创建 Host，也不创建第二个 session。

### 3.3 MainWindow injection

`MainWindow` 接收 Host，并将同一引用传给 `MainViewModel`：

- `editor_avalonia/MainWindow.axaml.cs:21-35`

MainWindow 不再拥有 `new EditorHostService()`。

### 3.4 MainViewModel injection

`MainViewModel` 构造函数要求外部 Host，并保留同一 Host 引用供 authoring operations 使用：

- `editor_avalonia/ViewModels/MainViewModel.cs:15-18`
- `editor_avalonia/ViewModels/MainViewModel.cs:434-438`

所有现有 Gate 临时 VM 均改为复用 gate VM 的 Host，不再创建第二个 Host。

### 3.5 Lifecycle split

`EditorHostService` 现在区分：

- `CloseProject()`：销毁当前 project/session，但 Host 仍可复用；
- `Dispose()`：terminal teardown，进入不可重建状态。

证据：

- `editor_avalonia/Services/EditorHostService.cs:552-600`
- `editor_avalonia/ViewModels/MainViewModel.cs:467-487`
- `editor_avalonia/ViewModels/MainViewModel.cs:929-934`

`MainWindow.OnClosed()` 停止 UI timer 并请求 VM 清理；Program 随后负责 Host terminal teardown：

- `editor_avalonia/MainWindow.axaml.cs:499-508`
- `editor_avalonia/Program.cs:15-16`

### 3.6 HRC-G1 gate

新增：

```text
editor_avalonia/Services/Hrc1Gate.cs
```

Gate 检查：

1. Window 与 ViewModel 共享同一 Host；
2. 初始没有隐式第二 session；
3. session 只能由 Host 创建；
4. `CloseProject` 不销毁 terminal Host；
5. VM dispose 不销毁 Host；
6. Host terminal dispose 幂等；
7. disposed Host 拒绝 session recreation；
8. disposed Host 后 VM 无法重新创建 session。

证据：`editor_avalonia/Services/Hrc1Gate.cs:8-67`。

---

## 4. HRC-G1 evidence

### 4.1 Commands and results

所有 Avalonia Gate 均使用串行等待方式执行，避免 WinExe 并发污染 scratch。

| Evidence | Result |
|---|---|
| `dotnet build editor_avalonia/AvaloniaEditor.csproj -c Debug --no-restore` | PASS，0 errors；4 个既有 warnings |
| `AvaloniaEditor.exe --hrc1` | `[HRC-G1 GATE] ALL GREEN` |
| `AvaloniaEditor.exe --selftest` | PASS |
| `--gate1` | PASS |
| `--gate2` | PASS |
| `--gate3a` | PASS |
| `--gate3b` | PASS |
| `--gate3c` | PASS |
| `--gate3d` | PASS |
| `--gate3g` | PASS |
| `cmake --build build --config Debug --target EditorBridge test_bridge` | PASS |
| `ctest --test-dir build -C Debug -R '^test_bridge$' --output-on-failure` | 1/1 PASS |
| `git diff --check` | 无 whitespace error；仅 LF/CRLF warning |

C# build 的 4 个 warning 是 HRC-1 之前已有的 nullable/unused-field warnings；HRC-1 未新增编译 warning。

ASan 环境仍会输出既有的 `interception_win` 提示，但本次所有 Gate 均正常退出并报告 GREEN。

### 4.2 Gate coverage

HRC-G1 覆盖 ownership/lifecycle，不替代 runtime integration evidence：

```text
PASS: Program/Host/Window/ViewModel identity
PASS: Host-owned session creation
PASS: CloseProject vs terminal Dispose
PASS: terminal idempotence
PASS: post-dispose rejection
PASS: existing authoring/runtime contract regression
NOT COVERED: Engine/GL bootstrap
NOT COVERED: production RuntimeTick
NOT COVERED: Viewport/picking/gizmo/readback/resize
```

---

## 5. Explicitly excluded work

以下内容不属于 HRC-1 Closure：

- `EditorSession.cpp` 的 runtime/viewport 行为修改；
- `Engine::Application` bootstrap；
- 持久 GL context/surface；
- RuntimeTick scheduler；
- Viewport FBO/readback；
- Picking；
- Gizmo；
- Resize；
- ECS 或 `test_ecs`；
- ImGui；
- Renderer/RenderGraph/RHI；
- Asset Pipeline；
- Inspector/Hierarchy 新功能。

HRC-1 只冻结 ownership 与 UI injection/lifecycle boundary。

---

## 6. Dirty-baseline handling

### 6.1 Pre-existing Phase 7 state

HRC-0 之前已经存在的 tracked Phase 7/Bridge/Engine 修改包括：

```text
bridge/include/editor_bridge/capi.h
bridge/src/EditorSession.cpp
bridge/src/EditorSession.h
bridge/src/capi.cpp
editor_avalonia/MainWindow.axaml
editor_avalonia/MainWindow.axaml.cs
editor_avalonia/Native/EditorBridgeApi.cs
editor_avalonia/Services/EditorHostService.cs
editor_avalonia/ViewModels/MainViewModel.cs
engine/include/Engine/Application.h
engine/include/Engine/Editor/EditorCamera.h
engine/include/Engine/Editor/ViewportPanel.h
engine/src/Core/Application.cpp
engine/src/Editor/EditorCamera.cpp
engine/src/Editor/EngineEditor.cpp
engine/src/Editor/ViewportPanel.cpp
engine/src/core/Application.cpp
```

这些 Phase 7 改动不能被归入 HRC-1 commit/tag。

### 6.2 HRC-1-owned additions/changes

HRC-1 新增或明确修改的 ownership/gate 文件为：

```text
editor_avalonia/App.axaml.cs
editor_avalonia/Program.cs
editor_avalonia/MainWindow.axaml.cs
editor_avalonia/ViewModels/MainViewModel.cs
editor_avalonia/Services/EditorHostService.cs
editor_avalonia/Services/Phase0SelfTest.cs
editor_avalonia/Services/Phase1Gate.cs
editor_avalonia/Services/Phase2Gate.cs
editor_avalonia/Services/Phase3Gate.cs
editor_avalonia/Services/Hrc1Gate.cs
docs/Editor-Avalonia-HRC1-Closure.md
```

其中 `MainWindow.axaml.cs`、`MainViewModel.cs`、`EditorHostService.cs` 同时包含 HRC-1 前已有的 Phase 7 差异，不能仅按文件边界拆分。

### 6.3 Prohibited cleanup actions

在获得明确授权前，不执行：

```text
git reset --hard
git clean
git stash
删除 spike 目录
清理或覆盖 Phase 7 assets
```

这些动作可能破坏 HRC-0 证据或丢失用户已有工作。

---

## 7. Freeze requirements before immutable tag

要完成真正的 HRC-1 immutable baseline，需要先完成以下之一：

### Option A — clean Phase 7 baseline

1. 由负责人确认并单独保存当前 Phase 7 dirty state；
2. 建立可复现的 Phase 7 baseline commit 或外部 patch；
3. 在该 baseline 上重新应用 HRC-1-only changes；
4. 重新运行 HRC-G1 和现有 Gates；
5. 创建 HRC-1 commit；
6. 创建 tag `engine-editor-avalonia-hrc1-host`。

### Option B — explicitly authorized combined snapshot

如果负责人明确授权将当前 Phase 7 + HRC-1 作为一个整体 snapshot，则必须改名为 combined snapshot，不能声称它是 HRC-1-only commit。

在 Option A 或 B 未完成前：

```text
HRC-G1 evidence: frozen as working-tree evidence
HRC-1 commit:    unavailable
HRC-1 tag:       unavailable
HRC-2:           not started
```

---

## 8. HRC-2 handoff

HRC-2 Runtime Bootstrap 的前置条件：

- HRC-1 ownership contract 保持不变；
- HRC-G1 继续 ALL GREEN；
- HRC-1 immutable baseline 问题已由负责人解决或明确接受；
- 不修改 Viewport/Picking/Gizmo/ECS/ImGui；
- RuntimeTick owner、Engine/GL context creation、teardown order 在 HRC-2 单独记录。

HRC-2 目标仍然是：

```text
one Host
  → one Engine/GL runtime
  → one EditorSession
  → explicit production RuntimeTick owner
```

在此之前，Avalonia UI 继续保持：

```text
PARTIAL / NOT FROZEN
```
