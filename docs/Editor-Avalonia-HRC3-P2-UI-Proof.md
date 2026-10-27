# HRC-3 P2 UI Proof — UI Contract / Audit Freeze + Implementation

> **阶段**：UI-HRC-3 / **P2 UI Proof**（第三层，收口 D1）
> **日期**：2026-09-28
> **状态**：`P2-UI IMPLEMENTED` — U1~U8 全绿
> **前置**：P1 CLOSED、P2-Native CLOSED（ABI v1.2）、P2-Managed CLOSED（M1~M7）
> **未开始 / 仍冻结**：D3 Pick、D4 Resize、A8

> **Superseded note（Commit A）**：本文记录的是 P2 UI Proof 当时的实现事实。
> 自 Commit A 起，产品 UI 的周期性 runtime pump 所有权已集中到
> `EditorHostService` 的 service timer；`MainWindow` 只消费 `FrameAdvanced`
> 并呈现，不再持有 production pump 定时器。`RuntimeTick()` 仅保留给
> gate / 诊断逐帧取证。`PumpOwnershipGate`（`--pumpowner`）已加入回归面。
>
> **端到端已验证**（2026-10-21，native 产物就位后）：
> `[PUMP-OWNER GATE] ALL GREEN`，`idle pumps=28 frames=28 rate=18.5/s`、
> `playing pumps=27 frames=27 rate=17.7/s`，`attempts == frames + failures` 恒等，
> Play/Stop 前后无第二 driver。证据与实现细节见
> `docs/Editor-Avalonia-Commit-ABC-Closure.md` §1。
> 本文其余部分（UI 呈现链、U1~U8）**仍然有效**，只有 pump ownership 一节被取代。

---

## 1. UI contract / audit freeze（先审计，后实现）

审计对象：`ViewportImage`、`_viewportBitmap`、`SupportsViewport`、pump owner。

### 1.1 审计结论（决定实现形态的三个事实）

| # | 事实 | 证据 |
|---|---|---|
| A1 | `ViewportImage` 是 `<Image x:Name="ViewportImage" Stretch="None"/>`，XAML 里**没有绑定 Source** | `MainWindow.axaml:90` |
| A2 | production EngineHost 后端下 **legacy 定时器从不启动** | `SupportsViewport => !IsEngineHost`；`MainWindow.axaml.cs` 的 `if (_host.SupportsViewport)` |
| A3 | legacy 绘制路径是 `OnViewportTimerTick` → `ViewportTick` + `GetViewportPixels()`（legacy `ViewportPanel` FBO），二者都是 `LegacyOnly`，在 EngineHost 后端**会抛异常** | `EditorHostService`：`ViewportTick` / `GetViewportPixels` 均 `LegacyOnly` |

**推论：改造前 production 后端的 UI 一直只显示 `ViewportPlaceholder`，从未显示过任何真实帧。**

### 1.2 第二个审计事实：production 后端**没有任何 pump owner**

全仓搜索 `RuntimeTick(` 的调用者：只有 `Hrc2Gate` 与 `P2ManagedFrameGate` 两个 gate。
**没有任何生产代码调用它。** 也就是说 production EngineHost 后端此前**从不产生帧**。

> ⚠ 因此 P2 UI 建立的是**第一个且唯一**的 pump owner —— 它调用的是**既有单一入口**
> `EditorHostService.RuntimeTick`。这**不是**"新增第二个 pump"：
> 旧设计的双源是 `ViewportTick`（viewport）+ `EditorSession_RuntimeTick`（session），
> EngineHost 设计把两者合并成了**一个**入口；P2 UI 恢复的正是这个**唯一**入口的驱动。

### 1.3 冻结的 UI 呈现链

```text
RuntimeTick / existing single pump      ← 既有唯一入口，未新增
        ↓
P2 Fetch packet                         ← 既有 op 28，未新增 ABI
        ↓
Managed PresentationFrame               ← 既有显式 offset 解码
        ↓
UI-thread WriteableBitmap               ← 新增（UI 层职责）
        ↓
ViewportImage.Source                    ← production 控件
```

### 1.4 硬边界（实现严格遵守）

| 禁止项 | 状态 | 机械核验 |
|---|---|---|
| 不新增 ABI | ✅ | ABI 仍 `((1<<16)\|2)`；本层 0 个新导出、0 个新 op/status |
| 不新增第二个 frame pump | ✅ | presenter 内 `EngineHost_PumpOneFrame` 命中 0；只经 `host.RuntimeTick` 这一个既有入口 |
| 不调用 GL / `glReadPixels` / FBO API | ✅ | presenter 内 `glReadPixels` / `OpenGL` / `GL_` 命中 **0** |
| 不复用 legacy `ViewportPanel` 路径 | ✅ | presenter 内 `ViewportTick` / `GetViewportPixels` / `EditorSession_` 命中 **0**；legacy `OnViewportTimerTick` 函数体**未改** |
| 不把"上一帧 bitmap"当成新帧 | ✅ | `PresentLatestFrame` 在 no-new-frame 时**直接 return false，一个字节都不碰控件**；U4 断言 `Image.Source` 引用不变 |
| 不在 UI 层自行生成 `frame_id` | ✅ | presenter 中 `FrameId` 的全部取值都是 `frame.FrameId`（L94/L134/L137），**无任何生成逻辑** |
| `frame_id` 随展示对象可追踪 | ✅ | `ViewportDisplayReceipt.DisplayedFrameId` 与 bitmap 实例一起携带 |
| 不扩大到 Resize | ✅ | 只验 800x600；无尺寸协商；D4 仍 Deferred |

---

## 2. 实现

### 2.1 新增文件

| 文件 | 作用 |
|---|---|
| `Services/ViewportFramePresenter.cs` | 呈现器 + `ViewportDisplayReceipt`（UI-side 回执） |
| `Services/P2UiProofGate.cs` | U1~U8 gate（`--p2ui`） |

改动：`MainWindow.axaml.cs`（production pump owner + presenter 注入 + `--p2ui` 入口）、
`PresentationFrame.cs`（新增 `ForOrderedReplayProbe`，**仅供 gate 构造"迟到旧帧"做乱序回放**）。

### 2.2 关键设计点

**上下翻转**：packet 是 GL 原生行序（左下角起），Avalonia `WriteableBitmap` 是左上角起，
因此逐行翻转。翻转关系**写进回执**（`VerticallyFlipped`），使 U1 能按声明关系**逐字节**比对，
而不是"看起来差不多"。

**不回退（U4 的可执行不变量）**：`Present` 在 `frame.FrameId <= _displayedFrameId` 时**拒绝呈现**。
这让"帧 N+1 先到、迟到的 N 后到"变成一个**同步可判定**的性质，不需要 delay / retry。

**UI 线程**：`Present` 强制 `Dispatcher.UIThread.VerifyAccess()`。

---

## 3. U1 ~ U8

| Gate | 证明了什么 |
|---|---|
| **U1** | packet → `PresentationFrame` → `WriteableBitmap`：把 displayed bitmap **逐字节读回**，按声明的翻转关系与**同一个 packet** 全面比对（任一字节不符即失败）；尺寸 800x600 |
| **U2** | production `ViewportImage.Source` **确实**指向该帧的 bitmap（`ReferenceEquals(image.Source, receipt.Bitmap)`），且 legacy `ViewportPlaceholder` 已被隐藏。起点断言 `Source is null` / placeholder 可见 ⇒ 证明状态真的被改变 |
| **U3** | `displayedFrameId == fetchedFrameId == packet.frame_id` 三者一致（present 前记 fetched identity，present 后比对 receipt） |
| **U4** | ①不 pump ⇒ no-new-frame，`Image.Source` **引用不变**、displayed 不前进；②把**更旧**的帧直接喂 presenter ⇒ **被拒绝**，UI 不回退、不换 bitmap；③连续 frame_id **严格单调** |
| **U5** | displayed 像素**非 clear-only**：`nonClear > 0`（用 P1 已验证的真实 gp01 fixture + 确定性计数） |
| **U6** | `--p2ui` 全绿 **6/6**；`--p2frame` 仍全绿 **3/3** |
| **U7** | `test_bridge` **44/44**（Debug+Release）、`test_hrc` 15/15、`test_scripting` 27/27、`dotnet build` exit 0、`git diff --check` 0、残留 0 |
| **U8** | **I2 仍 243**：本层新增 **0** 个 native 测试，`test_bridge` active 仍 44；managed gate 单独记录，不混入 native 计数 |

### 3.1 实测输出

```text
[P2-U1] frame=1 800x600 nonClear=7296 bound=True
[P2-U2] frame=2 800x600 nonClear=7296 bound=True
[P2-U3] frame=3 800x600 nonClear=7296 bound=True
[P2-UI U4] no-regress + no-stale-reuse OK (displayed=3, monotonic=1->2->3)
[P2-UI GATE] ALL GREEN
p2ui exit=0
```

> **`nonClear = 7296`** 与 P1 在 native 侧确立的 presentation FBO 像素数**完全一致**：
> atomic native frame → managed 解码 → UI bitmap，整条链路的确定性内容没有丢失或变形。

### 3.2 过程中修正的自身问题（实现语义未因此改变）

1. `PixelFormat` / `AlphaFormat` 在 **Avalonia 11.2.7** 位于 `Avalonia.Platform`
   （不是 `Avalonia.Media.Imaging`）；`WriteableBitmap` 才在 `Avalonia.Media.Imaging`。
   反射核实后修正。
2. `image.Source` 静态类型是 `IImage?`，不能直接赋给 `WriteableBitmap?`。
3. 乱序回放需要一个**指定** identity 的帧对象；为此给 `PresentationFrame` 加了
   `ForOrderedReplayProbe`，并注明**只有 gate 用**，生产帧一律来自 native packet。
4. 一次脚本化编辑把 `OnProductionFrameTick` 写到了 class 外，并留下 EOF 空行
   （`git diff --check` 抓到）——已修复，`diff --check` 现为 0。

---

## 4. 关闭陈述

> **D1 达成**：UI 显示的是**当前 EngineHost runtime frame**。
> atomic native fetch（v1.2，op 28）→ managed 显式 offset 解码 → UI 线程 `WriteableBitmap`
> → production `ViewportImage.Source`，`displayedFrameId == fetchedFrameId == packet.frame_id`，
> 显示像素非 clear-only 且与 native 侧计数一致（7296），连续帧不回退、不复用旧帧。

**P2 全链闭合**：Native contract → Managed mirror → **UI proof**。

---

## 5. 之后（各自独立、需单独批准）

- **D3 Pick**、**D4 Resize**、**A8**：继续 Deferred。
- 下一条自然延伸是 P3（viewport UI gate / `--p2ui` 纳入常规 gate 集合），
  但那属于范围决策，本层不擅自纳入。

---

## 6. standing 回归面（已完成）

P2 已升级为 production-path capability，`--p2frame` / `--p2ui` 由
**`tools/run_p2_gates.ps1`** 进入**常驻回归面**：

- 每场景独立进程（NHI-1 教训）；**无 retry / delay**（失败必须保持失败）；
- GREEN 判据 = exit 0 **且** stdout 含对应 marker；
- **必须用 `pwsh`（PowerShell 7）运行** —— `$psi.ArgumentList` 在 PS 5.1 不存在；
- 实测 **3/3 GREEN**（9~24s/次）；
- **0 native inventory 增量** ⇒ `test_bridge` 仍 44、**I2 仍 243**、ABI 仍 v1.2；
- 本 slice **未改动任何 runtime 行为**（只做 gate wiring + 文档）。

详见 `docs/Evidence-Baseline.md` 的「P2 进入 standing 回归面」小节。

