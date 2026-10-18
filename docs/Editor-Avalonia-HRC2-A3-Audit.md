# Editor Avalonia HRC-2-A3 — Persistent GLFW Context Audit

> **阶段**：UI-HRC-2-A / A3  
> **日期**：2026-09-24  
> **状态**：`COMPLETE / RELEASED`  
> **前置**：`docs/Editor-Avalonia-HRC2-A2-Closure.md`  
> **HRC-2-A 状态**：`IN PROGRESS`  
> **HRC-G2P-01~08**：`NOT GREEN`

---

## 1. Hybrid ownership decision

仓库内 GLFW 文档规定：初始化、终止、窗口/context 创建销毁和 event processing 必须在 main thread；context current/swap/current 查询可在已初始化后由其他线程使用。

因此 A3 采用：

```text
creation/UI thread
  ├─ register main thread
  ├─ glfwInit
  ├─ create persistent hidden window/context
  ├─ GLAD load
  ├─ runtime thread handoff
  ├─ join runtime thread
  └─ destroy carrier / glfwTerminate

dedicated runtime thread
  ├─ make persistent context current
  ├─ Engine/GL/Session work
  ├─ frame/tick/render
  └─ un-current at frame/shutdown boundary
```

这保留 ONE dedicated runtime thread 作为 Engine/GL owner，同时不违反 GLFW main-thread lifecycle contract。

---

## 2. Implemented in A3

### Persistent factory

`OpenGLGraphicsFactory` 新增：

- persistent hidden window/context create；
- GLAD readiness；
- make/un-current；
- current probe；
- owner-thread carrier destruction；
- creator-side carrier teardown；
- shared `GlfwLifetime` acquire/release；
- init/terminate counters；
- legacy `CreateWindow` 异常路径使用局部 context/window rollback，live-window 计数只在完整构造后递增；
- persistent teardown failure injection 与 teardown state probe。

Legacy `CreateWindow()` 入口保留，但 persistent mode 下 fail-closed；OpenGL client hint 显式恢复为 OpenGL path。

### EngineHost handoff

- `EngineHost_Create` 要求已登记的 main/creation thread；
- carrier 在 creation thread 创建；
- runtime bootstrap command 负责 make-current；
- runtime frame command只使用已转移的 context；
- normal shutdown：runtime un-current → join → creator carrier destroy；
- emergency teardown：runtime 只释放 runtime-side current state，creator-side carrier teardown 由 `Shutdown` 完成；
- graphics state probe路由到 runtime thread；
- worker-thread `EngineHost_Create` 返回 typed `WrongThread`；
- startup/shutdown lifecycle 使用 5 秒 bounded wait；超时记录 `lifecycleTimedOut`，`Destroy` 对仍存活的 runtime fail-fast，避免无界 join；
- bootstrap 使用内部 result buffer 与 5 秒 deadline，不把 caller result 暴露给超时 runtime；
- graphics state/counters、last-frame、diagnostic probes 改为锁/原子 snapshot，不通过 runtime command queue；
- emergency/runtime teardown 通过 `EngineHost_GetTeardownState` 独立观测；
- runtime exception 与 graphics teardown failure 均有 failure-injection evidence；
- legacy `EditorSession_*` C ABI 全量 exception boundary；
- C ABI `EditorSession_Create` 要求已注册 main thread；
- owner registration mismatch fail-closed 且不污染 declared state。

### GLFW lifetime

`GlfwLifetime` 现在由 `OpenGLGraphicsFactory` 和 `RHIWindow` 共用，按 legacy/persistent mode 隔离；同时记录实际 init/terminate 次数和 owner thread。EngineHost persistent mode 下 legacy `EditorSession_Create` 和 `CreateWindow` 均 fail-closed；production C ABI 创建 session 必须先完成 main-thread registration。`GlfwWindow`/RHI 的状态查询、event、swap 与 teardown 均增加 owner-thread fail-closed 检查；`Application` 的 `SubsystemManager` 已移动到其回调所依赖资源之后声明。

---

## 3. Evidence currently passing

```text
cmake --build build --config Debug --target EditorBridge test_bridge test_hrc
ctest --test-dir build -C Debug -R "^(test_bridge|test_hrc)$" --output-on-failure
```

通过内容包括：

- persistent context Ready/current probe；
- creator un-current → runtime make-current；
- runtime un-current → shutdown → carrier clear；
- shared lifetime users/init/terminate counters；
- make/un-current counters；
- worker-thread EngineHost_Create rejection；
- persistent-mode legacy `EditorSession_Create` rejection；
- explicit main-thread registration；
- pure C header compile smoke (`gcc -std=c11 -c`)；
- H1 destroy → H2 create 同进程重启；
- runtime result/diagnostic/last-frame regression；
- runtime command exception → emergency teardown → typed `ShutdownFailed`；
- bootstrap current failure → bounded rollback；
- graphics carrier teardown failure → independent teardown state；
- legacy `CreateWindow` exception rollback；
- legacy C ABI exception injection stays inside the C ABI；
- worker-thread legacy `EditorSession_Create` rejection；
- legacy `EditorSession` bridge tests。

HRC-1 bounded regression：

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

managed build：0 errors；最新复跑 0 warnings。

验证 runner 对每个进程设置 120 秒硬超时；最新 native、managed、pure-C smoke 与逐 Gate bounded 复跑均 exit 0。`--hrc1`、A6 批量 runner 中的 `--gate2`/`--gate3a` 曾出现 runner-level transient timeout，但各 gate 随后独立 120 秒复跑通过；watchdog 已终止超时进程。

---

## 4. A3 closure and deferred scope

A3 persistent carrier 当前没有技术 blocker，以下事项明确延期到后续切片：

1. finite domain commands 仍通过 generic `Submit()` 同步等待；A3 已实现 operation 均要求 finite/bounded，长 operation cancellation 延后到 A4/A5/A6 的真实 Engine/Session/FBO command 接入。
2. direct C++ `OpenGLGraphicsFactory`/`RHIWindow` legacy construction 仅保留为 sandbox/compatibility 入口；production C ABI 已强制 registration，且 legacy/persistent mode 不可共存。完整 legacy deprecation 延后到 A6/A7。
3. Host-owned FBO、Session runtime binding、Application lifecycle/resource teardown 属于 A4/A5/A6。
4. CI/headless Windows/Xvfb matrix 属于 A8。

这些延期项不改变 A3 persistent carrier 的 release approval；HRC-2-A 整体仍未完成。

---

## 5. Decision

```text
A0 baseline:             COMPLETE
A1 ABI skeleton:         COMPLETE
A2 thread/queue:         COMPLETE
A3 persistent GLFW:      COMPLETE / RELEASED
A3 release:               APPROVED
A4 Application/FBO:      READY TO START
HRC-G2P-01~08:           NOT GREEN
```

A3 已完成 persistent carrier、owner/affinity、failure rollback、C ABI boundary 与 teardown evidence；下一步进入 A4 Application lifecycle split。

## 6. A4 handoff checkpoint

A4 已建立最小可执行 native proof loop：

```text
EngineHost
  → headless Application
  → persistent hidden GL
  → 64x64 Host-owned FBO + color readback
  → runtime-bound EditorSession
  → 3 frames / typed RuntimeResult
  → Stop/Close/Shutdown
  → same-process H1 → H2 repeat
```

当前已注册 `test_hrc`，覆盖三次循环、wrong-thread shutdown、FBO/session failure rollback、project retry/active-session transaction、typed RuntimeTick/Reload fault、dt clamp、颜色/GL-error readback、callback detach、Session/global proof 和 exactly-once stop。`test_bridge` 另覆盖 emergency no-current teardown、legacy runtime stats。Windows 本地 `test_bridge`(41) / `test_hrc`(9) 已通过；CI 已改为显式构建 HRC/Sandbox 目标，并在同一 Xvfb 会话执行两项测试、上传 `LastTest.log`，但全平台实跑前不把 G2P-08 标为 GREEN。

因此：

```text
A4 native implementation: COMPLETE
A6 native hardening:     COMPLETE
HRC-G2P-01~08:            NOT GREEN
HRC-2-A readiness:       NOT READY
```

Viewport、Picking、Gizmo、ECS、ImGui、Asset Pipeline 与 Avalonia production Host 切换均未纳入 A4。
