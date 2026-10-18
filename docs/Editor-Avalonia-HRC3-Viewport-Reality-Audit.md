# HRC-3 Step 1 — Viewport Reality Audit + Contract Freeze

> **阶段**：UI-HRC-3 / Step 1（**审计 + contract 冻结，零代码改动**）
> **日期**：2026-09-28
> **状态**：`AUDIT COMPLETE / CONTRACT PROPOSED`（§6 五条决策待 ratify）
> **前置**：`docs/Editor-Avalonia-HRC2-Closure.md`（HRC-2 CLOSED，Windows hidden-GLFW scope）
> **性质**：HRC-3 **不是继续完善 UI**，而是验证 HRC-2 有意没有声称的 rendering reality
> **不吸收**：任何代码改动、任何 legacy 能力回接、任何 A7 routing/pump/ownership 变更

---

## 1. 一句话结论

**当前生产路径（EngineHost）不存在"渲染真实呈现"**：render target 是一个固定 64×64 的
**清屏 + 单像素校验**探针，**没有场景绘制、没有帧回读、没有 present 通道**；
UI 侧的 viewport 是占位 `Border`+`Image`，在 EngineHost 模式下**永远不会被写入像素**。
legacy 路径有真实 render→FBO→`glReadPixels`→`WriteableBitmap` 链路，但它是 **CPU 回读、
legacy-only、非生产路径**，且 **pick / texture handle 是 stub**。

---

## 2. 审计事实（逐条带出处）

### 2.1 生产路径（EngineHost）确实存在的

| 事实 | 出处 |
|---|---|
| hidden persistent GL context，**固定 64×64** | `bridge/src/EngineHost.cpp:300` `InitializePersistentHiddenContext(64, 64)` |
| offscreen FBO **64×64**，仅在初始化时 resize 一次 | `EngineHost.cpp:509` `m_OffscreenFbo->Resize(64, 64)`；无其他 resize 调用点 |
| 每帧流程：MakeCurrent → `Application::PumpOneFrame(dt)` →（playing 时）`Session::RuntimeTick` → **`m_OffscreenFbo->Clear(0.1,0.2,0.3,1)`** → **`ReadColorPixel(32,32)`** 与 (26,51,77,255) 比对 → Uncurrent | `EngineHost.cpp:1092-1173` |
| readback 只是**单像素 clear-color 探针**，不是帧内容回读 | `EngineHost.cpp:1143-1166`（`m_ReadbackVerified = true` 仅在此处） |
| proof 导出暴露 `fboReady/fboId/fboWidth/fboHeight/readbackVerified/frameCount/tickCount` | `EngineHost_GetRuntimeProofState`；`test_editor_capi.cpp:477-493` |
| 该测试断言 `readbackVerified == 0`（尚未 pump 帧） | `test_editor_capi.cpp:490` |

### 2.2 生产路径**不存在**的（关键）

| 缺失 | 证据 |
|---|---|
| **FBO 内没有场景绘制** | 帧内唯一的绘制动作是 `Clear`；场景渲染回调 `m_SceneRenderCallback` 只存在于 legacy `ViewportPanel`（`ViewportPanel.cpp:208`） |
| **没有 present / 像素 / 纹理导出** | 24 个 `EngineHost_*` 导出中**没有任何**返回像素、纹理句柄或 present 的接口（清单见 `capi.h`） |
| **UI 侧 viewport 在生产模式无数据源** | `MainWindow.axaml.cs:45` `if (_host.SupportsViewport)` 才启动 viewport timer；EngineHost 走 `else` 分支去订阅 `PendingEditCompleted`（A7-C03）。`SupportsViewport => !IsEngineHost`（`EditorHostService.cs:206`） |
| **UI 侧 viewport 是占位** | `MainWindow.axaml:74-94`：`Border` + `Image(ViewportImage)` + `ViewportPlaceholder`，注释写明"真实呈现需要后续 NativeControlHost 或手动纹理回读" |
| **生产模式无 viewport/pick/gizmo 能力** | 全部 `LegacyOnly(...)`，抛 `HostCapabilityNotAvailableException("Viewport/Gizmo/Picking is out of A7 scope and has no EngineHost equivalent")`（`EditorHostService.cs:407-432`，消息在 `:472/:481`） |
| **生产模式无 resize** | 无 `EngineHost_SetViewportSize`；FBO 尺寸只在初始化设定 |

### 2.3 legacy 路径（真实但非生产）

| 事实 | 出处 / 性质 |
|---|---|
| `ViewportTick` → 相机输入 flush + `ViewportPanel::Update`（场景回调 + grid overlay）→ **`glReadPixels` 从 `m_FBO_ID` 回读**到互斥量保护的 `m_ReadbackPixels` | `ViewportPanel.cpp:195-235`；**真实** |
| `GetViewportPixels` → 输出 `[int32 w][int32 h][RGBA]`；managed 侧 `Marshal.Copy` + Y 翻转写入 `WriteableBitmap` | `EditorSession.cpp:1154+`；`MainWindow.axaml.cs:419-460`；**真实** |
| `GetViewportTextureHandle` → **恒返回 0，注释 TODO "GPU interop 待实现"** | `EditorSession.cpp:941-947`；**stub** |
| `ViewportPick` → **恒返回 -1，注释 TODO "集成渲染后接入 ViewportPanel 的 ID 拾取"** | `EditorSession.cpp:1103-1109`；**stub** |
| `PickingFBO`（`Resize/Bind/Unbind/EncodeID/ReadPixel`）**实现完整但完全未接线** | 仅被自身 `.cpp/.h` 引用，全仓无第二个引用点；**unwired** |
| gizmo 仅存状态：`SetGizmoMode/Space/Snap` 写 `m_Gizmo*`；`ViewportSerializer` 持久化 `GizmoMode`；`EditorTools.cpp` 做 space→index 映射 | **无任何证据**表明存在被渲染的 gizmo 或 gizmo 改变 transform；**data-only** |
| `SetViewportSize` 链路存在（session → `vp.GetCamera().SetViewportSize`）但 **UI 从不调用** | `EditorSession.cpp:954-966`；`MainViewModel.SetViewportSize` 无调用点；**dead from UI** |

### 2.4 现有 viewport 证据的真实边界

- **唯一的 viewport gate 断言是"拒绝"**：`Hrc2Gate`（`EditorHostService.cs:72-76` 区域）断言
  `ViewportTick` / `ViewportPick` / `SetGizmoMode` 在生产 backend 抛 `HostCapabilityNotAvailableException`。
  即 HRC-2 证明的是"**生产路径没有 legacy 泄漏**"，而**不是**"viewport 工作"。
- **没有任何 gate 断言像素内容、pick 结果或 gizmo 行为**（全仓搜索 `GetViewportPixels` /
  `ViewportPick` / `SetGizmoMode` 的断言使用点：0）。
- legacy gate 运行期间 30 FPS 的 viewport paint loop 确实在跑且**不抛异常**，但
  "不抛异常" ≠ "内容正确" —— 像素内容零断言。
- A1 建立的 Camera **组件**（`AddComponent(idx,"Camera")` / `CameraZoom` / `CameraViewport` /
  `CameraBounds`）是**数据契约**；生产路径不做场景渲染，因此它**没有渲染消费者**。

---

## 3. 能力矩阵（Step 1 判定）

分类口径：**可证明**＝今天就能用可执行 gate 取证；**仅 API/代码路径**＝代码存在但无可执行证据；
**stub**＝代码显式 TODO/恒定返回；**mock/legacy-only**＝真实但被生产路径显式排除。

| 能力 | 分类 | 依据 |
|---|---|---|
| 生产 render target 存在且尺寸/身份可查询 | **可证明** | `GetRuntimeProofState`（今天就能断言 `fboId/64×64`） |
| 生产 FBO 的单像素 clear-color 校验 | **可证明** | `readbackVerified` 翻转为 1（pump 一帧后） |
| 生产**场景绘制**进 FBO | **不存在** | 帧内只有 `Clear` |
| 生产**帧内容回读** | **不存在** | readback 是单像素探针 |
| 生产 **present 到 UI** | **不存在** | 无像素/纹理导出；timer 不启动；占位 Image |
| UI 显示的像素与"当前 runtime frame"一致 | **不存在** | 无 present 通道，无法成立 |
| resize 后 attachment/viewport 一致 | **不存在**（生产）/ **仅 API**（legacy，且 UI 不调用） | 见 §2.2 / §2.3 |
| legacy 场景渲染 + FBO 回读 + `WriteableBitmap` 呈现 | **mock/legacy-only** | §2.3；内容零断言 |
| legacy pick | **stub** | 恒 -1 + TODO |
| GPU 纹理句柄互操作 | **stub** | 恒 0 + TODO |
| ID-buffer 拾取基础设施 | **unwired**（实现完整未接线） | `PickingFBO` 无引用点 |
| gizmo | **data-only**（无渲染/无 transform 应用证据） | §2.3 |
| camera（legacy `EditorCamera`） | **mock/legacy-only** | 真实但被生产路径排除 |
| camera（生产 `Camera` 组件） | **仅 API/代码路径** | 数据契约，无渲染消费者 |
| HRC-2 single pump 契约 | **已冻结，不重开** | A2 + `--boot` + `SinglePumpOwnerAndLifecycleRegression` |

**净结论**：HRC-3 若要"证明 viewport 真的工作"，需要**新增能力**（present 通道 + 场景绘制进生产
FBO + pick 实现），而不是补证据。**这必须先由 §6 决策拍板，不能由实现者默认选择。**

---

## 4. Contract 冻结（Step 1 提议，ratify 后生效）

### VR-1 Render target reality
生产 render target 必须是**视口尺寸**（非固定 64×64 探针），生命周期归 Host：
create → resize → render → readback/present → destroy，且 resize 后 attachment/viewport 同步更新。
判定必须包含"UI 控件尺寸变化 → FBO 尺寸变化 → 下一帧内容按新尺寸产生"的可执行证据。

### VR-2 FBO/readback ↔ UI consistency
UI 显示的像素必须来自**同一 runtime frame**，可证明为：
`frame_id(N) → readback → presented`，且 `presented.frame_id == N`（不是"最新的一张"这种含糊说法）。
必须能区分"陈旧 texture"（上一帧/静态图）与"当前帧"。

### VR-3 Viewport interaction
pointer → pick → selection；gizmo → runtime transform；camera sync。
每一步必须标注线程边界（UI thread → runtime thread）与同步点。
**pick 必须返回真实 ID**（`PickingFBO::EncodeID/ReadPixel` 是现成实现，或另选方案并说明），
不接受恒 -1 / 恒 0。

### VR-4 Frame/pump consumption
HRC-2 的 single pump / Host ownership / thread boundary / no legacy fallback **保持冻结**：
HRC-3 只证明"viewport 消费这个已冻结的 frame contract 是否真实"，
**不得**为让 viewport 工作而新增第二个 tick 源、旁路 pump 或把 legacy 路径接回 EngineHost。

### VR-5 能力矩阵入证
每个 viewport 能力必须在矩阵里有明确分类与证据入口；
"不抛异常"不算证据，"接口存在"不算证据。

---

## 5. 明确禁区

1. **禁止**因为发现 legacy `ViewportTick` / `ViewportPick` / Gizmo 存在，就把 legacy 路径接回
   EngineHost。HRC-2 已冻结：唯一 pump / Host ownership / thread boundary / no legacy fallback。
   HRC-3 的问题是"在这些约束下 viewport 是否真的工作"，不是"怎么让它工作"。
2. **禁止**重开 A7 routing contract（`EngineHost_Dispatch/Fetch` 的 27 op 路由）。
3. **禁止**把 §2.4 的"拒绝断言"扩写成"viewport 已验证"。
4. **禁止**用 A8（Linux/Xvfb）为 HRC-3 背书；A8 保持完全独立、Deferred。
5. 若实现需要新增 native 导出（如 present/readback/resize），必须**独立 ABI slice**：
   单独冻结版本号、单独 native + managed mirror 证据、单独重跑 I2 与 native 套件。

---

## 6. 待 ratify 的 5 条决策

| # | 决策 | 建议 | 代价 / 影响 |
|---|---|---|---|
| **D1** | HRC-3 的目标定义 | **"UI 真的显示 runtime frame"**（含 present 通道），而不是"只证明 FBO/readback plumbing" | 若只做 plumbing，HRC-3 会重复 HRC-2 的 G2P-01 结论而无新增真实性 |
| **D2** | present/readback 机制 | **CPU readback → Avalonia `WriteableBitmap`**（复用 legacy 已验证的呈现方式，零新 ABI）作为第一版；shared texture/GL interop 留作后续 | CPU 回读有带宽成本；D2 决定是否需要新 ABI（见 §5.5） |
| **D3** | pick 实现 | **接线现成的 `PickingFBO`**（EncodeID + ReadPixel，已完整实现），ID buffer 语义随帧清理 | 需要把 ID 编码纳入生产帧；`PickingFBO` 目前零引用，接线属于新增 native 改动 |
| **D4** | resize 语义 | **FBO 跟随 UI 控件尺寸**，resize 后下一帧起 attachment/viewport 同步；resize 事件不得打断 HRC-2 的 pump 契约 | 需要 resize 信号通道（UI → Host），可能又是新导出 |
| **D5** | 证据环境 | **Windows hidden-GLFW 单机**（与 HRC-2 同范围）；A8 继续独立 Deferred | 跨平台呈现 receipt 仍缺，但与 HRC-2 保持同一证据纪律 |

---

## 7. Step 1 判定

```text
audit:                    COMPLETE（零代码改动）
contract (VR-1~VR-5):     PROPOSED（§6 D1~D5 待 ratify）
capability matrix:        冻结（§3，含 5 类"不存在"与 2 类 stub）
禁区:                     冻结（§5，含"禁止 legacy 回接"）
HRC-2 契约:               不重开（pump / ownership / thread boundary / no legacy fallback）
A8:                       DEFERRED，完全独立
next:                     ratify D1~D5 → 才进入 HRC-3 Step 2（能力实现 + 证据）
```

**未做任何代码改动**：本 Step 只读代码 + 写文档。
顺带更正了 HRC-2 闭包文档中 G2P-01 的一处过强表述（"readback verified" → 单像素 clear-color
探针 + 固定 64×64），该更正不改变 G2P-01 依其自身措辞的 GREEN 判定。
