# Editor Avalonia — Slice E Step 1：动态 Viewport 尺寸与 FBO 生命周期审计

> **阶段**：Slice E / **Step 1（Audit + Contract Freeze）**，**零代码**
> **日期**：2026-10-21
> **状态**：`E STEP-1 CLOSED` — 尺寸链已定性；**1 项 ABI 决策待批**（§7）
> **前置**：D Step 3 CLOSED（ABI v1.4）
> **纪律**：本 Step 不修改任何 native / managed / ABI / gate；先审计，再决定最小实现范围

---

## 1. 结论摘要

1. **resize 目前完全没有接线**：`editor_avalonia/` 中 `SizeChanged` 命中 **0**。
   控件缩放（实测窗口较窄时控件只有 `554×600`）根本不会到达 native。
2. **帧尺寸被三重钉死在 800×600**：FBO 创建尺寸、fetch 的尺寸等值校验、
   managed 的解码与分配。**不是一处常量，而是四层共用的同一个假设。**
3. **但渲染与 pick 的几何已经是尺寸无关的**：`RenderProductionFrame` 每帧按
   `targetWidth/targetHeight` 重算相机与宽高比，`PumpOneFrame` 按 FBO 尺寸设 viewport，
   `ReadPickPixel` 按 FBO 的 `m_Width/m_Height` 做越界判定。
   ⇒ E 不需要重写渲染，**只需要打通"尺寸输入 + 容量"两条通道**。
4. **packet 头已经带 `width/height`**（offset 0/4）⇒ **消息格式不需要改**。
   被钉死的只是**容量**与**校验**。
5. **EngineHost 没有任何 resize 通道**：legacy 有 `EditorSession_SetViewportSize`，
   但那是 NHI-1 禁止路径；EngineHost 侧既无 op 也无 export。
   ⇒ **E 必须新增一个 op**（append-only），这是本次审计唯一的 ABI 决策（§7）。
6. `OpenGLFramebuffer::Resize` 的现状有两个 E 必须正面处理的行为：
   **静默把 0 夹到 1**、以及**先 Destroy 再创建**（创建失败会丢掉旧帧）。

---

## 2. 完整调用链（现状，逐边界标注"单位 / 来源 / 时机"）

```text
[不存在] Avalonia SizeChanged
    │  ← 全仓 0 命中；控件尺寸变化不会产生任何信号
    ▼
MainWindow.RequestPickAtPoint
    │  单位：DIP，相对 ViewportImage
    │  来源：e.GetPosition(image)（事件时刻）
    ▼
ViewportPickMapper.TryMap(..., frameWidth=800, frameHeight=600)
    │  单位：帧像素
    │  来源：PresentationFrame.ExpectedWidth/Height —— **编译期常量**
    ▼
EditorHostService.RequestPick → EngineHost_Dispatch(PICK, [x,y])
    │  时机：UI 线程；只登记，不渲染
    ▼
EngineHost::PumpOneFrame（runtime 线程，context current）
    │  1. MakeRuntimeContextCurrent
    │  2. DrainPendingEdits
    │  3. SetProductionPickPassEnabled(有 pending pick 时)
    │  4. PresentationFbo->Clear(); Bind(); Viewport(0,0, FBO w, FBO h)
    │  5. Application::RenderProductionFrame(..., FBO w, FBO h)
    │       └ 每帧按 aspect = w/h 重算 OrthographicCamera（尺寸无关）
    │  6. ServicePickRequestForFrame() → PresentationFbo->ReadPickPixel(x,y,...)
    │       └ 越界用 FBO 的 m_Width/m_Height 拒绝
    │  7. UncurrentPersistentContext; ++m_FrameId
    ▼
FrameAdvanced → MainWindow.OnFrameAdvanced
    ▼
EditorHostService.TryFetchPresentationFrame
    │  分配：new byte[EngineHostFramePacketBytes]  ← **每帧 1.92 MB**（§6 A9）
    ▼
EngineHost::FetchPresentationFrame
    │  校验 1：FBO w/h 必须 == 800/600，否则 reject
    │  校验 2：capacity >= 1,920,016，否则 reject
    │  无新帧判定：m_FrameId == m_LastFetchedFrameId
    │  读回：ReadPixels(0,0, w, h) 写进 outBuffer + 16
    │  header：w/h/frame_id 与像素同一次 fetch
    ▼
PresentationFrame.TryDecode
    │  校验：length == 1,920,016 且 w/h == 800/600，否则拒绝
    ▼
ViewportFramePresenter.Present
    │  尺寸变化时**会**重建 WriteableBitmap（已具备动态能力）
    ▼
ViewportImage.Source
    │  几何前提：Stretch=None + Left/Top ⇒ 控件原点锚定 1:1
    ▼
[用户下一次点击] → 回到 RequestPickAtPoint（此时 frameWidth 仍是 800/600 常量）
```

**结论**：链路上唯一"已经动态"的是 **渲染/呈现/位图重建**；
唯一"静态"的是 **FBO 创建尺寸、fetch 校验、容量、managed 解码、mapper 入参**。

---

## 3. 三种尺寸的实际来源

| 尺寸 | 现状来源 | 单位 | 谁拥有 | 现状是否可信 |
|---|---|---|---|---|
| **控件尺寸** | Avalonia 布局（`ViewportImage.Bounds`） | DIP | UI | 可信，但**从不外传** |
| **FBO 尺寸** | bootstrap 时的 `ENGINE_HOST_PRESENTATION_WIDTH/HEIGHT` | 帧像素 | native（`m_PresentationFbo`） | 可信；且 `Resize` 已存在但**只在创建时调用一次** |
| **帧尺寸** | packet header（offset 0/4）= fetch 时 FBO 的尺寸 | 帧像素 | native | 可信，但 fetch 强制它 == 常量 |

**冻结（E-C1）**：三者的权威定义与优先级

```text
FBO 尺寸        = 渲染目标尺寸（native 唯一真相）
帧尺寸          = 该帧产生时 FBO 的尺寸（由 packet header 携带，**不是**由控件推算）
控件尺寸        = 布局事实，只用于**计算显示矩形**，不参与渲染与 pick 坐标系
显示矩形        = 控件内实际绘制帧的区域（由呈现模式决定，见 E-C5）
```

> `PresentationFrame.ExpectedWidth/ExpectedHeight` 在 E 之后**不得再代表"帧的尺寸"**，
> 只能代表"默认/基准尺寸"。帧的尺寸一律取 packet header。

---

## 4. Packet 格式、容量与 managed 解码（E-C2）

### 4.1 格式：无需改动

```text
offset 0  int32  width       ← 已存在
offset 4  int32  height      ← 已存在
offset 8  uint64 frame_id
offset 16 uint8  rgba[w*h*4]
```

header 已经能表达任意尺寸。**E 不新增/不重排任何字段。**

### 4.2 容量：必须从"the size"变成"the max size"

现状：

```c
#define ENGINE_HOST_PRESENTATION_WIDTH 800
#define ENGINE_HOST_PRESENTATION_HEIGHT 600
#define ENGINE_HOST_FRAME_PACKET_BYTES (16 + 800*600*4)   // 1,920,016
```

这条常量同时承担了三个语义：**默认尺寸 / 最大尺寸 / 分配容量**。
E 必须把它们拆开：

| 语义 | E 之后的形态 |
|---|---|
| 默认尺寸 | 仍是 800×600（保证既有 P2/P1 证据面不变） |
| 最大尺寸 | 新增上限（建议 1920×1080），决定容量 |
| 分配容量 | `16 + MAX_W*MAX_H*4`（1920×1080 ⇒ 8,294,416 B） |

**冻结**：缓冲区按**最大容量**一次性分配；实际帧按 header 的 `w/h` 解码。
不再"每帧按固定大小重新分配"（§6 A9）。

### 4.3 managed 解码：从"等值校验"改为"范围校验"

`PresentationFrame.TryDecode` 现在的两条硬约束必须改：

| 现状 | E 之后 |
|---|---|
| `length != ExpectedBytes` ⇒ 拒绝 | `length < Header + w*h*4` ⇒ 拒绝（允许 fetch 只返回实际字节） |
| `w/h != 800/600` ⇒ 拒绝 | `w/h <= 0` 或 `> MAX` ⇒ 拒绝；否则按 header 解码 |

**冻结（E-C2）**：解码的**唯一**尺寸来源是 packet header；
`ExpectedWidth/Height` 只作为默认值与上界校验的一部分。

---

## 5. `ViewportPickMapper`：最小改动（E-C3 / E-C4）

### 5.1 现状签名

```csharp
static PickMapStatus TryMap(
    double pointerX, double pointerY,
    int frameWidth, int frameHeight,
    out PickPoint point);
```

现状成立的前提是 **"帧 1:1 绘制在控件原点"**（`Stretch=None` + `Left/Top`），
该前提在 View 里检查（`image.Stretch`、`Source.PixelSize`、`IsOriginAnchoredOneToOne`）。

### 5.2 现状没有封装"显示区域计算"

- View 用 `e.GetPosition(image)` 得到**相对控件**的点；
- 因为 1:1 且原点锚定，所以"相对控件的点" == "相对帧的像素"；
- **没有任何显示变换（offset / scale）参与**，也**没有**把这块逻辑重复放进 mapper。

⇒ 用户的担忧成立但方向相反：**不是 mapper 重复了显示变换，而是它完全不知道显示变换**。
一旦启用缩放，mapper 会静默按 1:1 映射 ⇒ 系统性错位。

### 5.3 冻结的新签名

```csharp
public static PickMapStatus TryMap(
    double pointerX, double pointerY,            // 相对承载控件的指针
    double displayedX, double displayedY,         // 完整图像矩形在控件坐标系的原点
    double displayedWidth, double displayedHeight,// 图像实际绘制尺寸（可 < / > 帧尺寸）
    int frameWidth, int frameHeight,              // 帧真实尺寸（来自该帧 header）
    out PickPoint point);
```

映射（每个轴独立，天然支持非等比）：

```text
scaleX = displayedWidth  / frameWidth
scaleY = displayedHeight / frameHeight
x = floor((pointerX - displayedX) / scaleX)
y = floor((pointerY - displayedY) / scaleY)
```

### 5.4 冻结的拒绝条件（全部 `GeometryMismatch` / `InvalidCoordinate` / `OutsideFrame`）

| 条件 | 状态 |
|---|---|
| 任一输入非有限 | `InvalidCoordinate` |
| `frameWidth/Height <= 0` | `GeometryMismatch` |
| `displayedWidth/Height <= 0` | `GeometryMismatch` |
| `scaleX/scaleY <= 0` 或非有限 | `GeometryMismatch` |
| 映射后落在 `[0,W)×[0,H)` 之外 | `OutsideFrame`（**不 clamp**） |

### 5.5 关于"裁剪"与"可见区域"（用户特别指出的点）

**冻结**：mapper 只接受**完整图像矩形**，不接受"控件可见裁剪区"。

- `Stretch=None` + `Left/Top` 且控件比帧窄时：图像被裁剪，但
  **控件原点仍是图像原点**、比例仍是 1 ⇒ 传入 `displayed = (0,0,frameW,frameH)`
  依然得到正确映射（`e.GetPosition` 永远不会返回超出控件范围的点，所以裁剪区自然不可达）。
- 若未来改为中心对齐/缩放：View 负责算出 letterbox 矩形（含负 offset），
  传给 mapper。**mapper 不猜呈现模式**。
- **禁止**用"控件尺寸 == 帧尺寸"来推断是否缩放（D 的 Step 2 已经踩过一次：
  窗口较窄时控件 554×600 ≠ 帧 800×600，但那不是缩放）。

---

## 6. 审计发现（必须记录，含两个"顺手可修但不属 E"的项）

| # | 发现 | 影响 |
|---|---|---|
| **A1** | `editor_avalonia/` 中 `SizeChanged` 命中 0 | resize 完全未接线 |
| **A2** | presentation FBO 只在 bootstrap 创建一次（`EngineHost.cpp:557`） | 尺寸永不变化 |
| **A3** | `FetchPresentationFrame` 对 FBO 尺寸做**等值**校验（`EngineHost.cpp:2262`） | 即使接了线，UI 也收不到非 800×600 的帧 |
| **A4** | packet header 已带 `w/h` | **格式无需改动** |
| **A5** | `ENGINE_HOST_FRAME_PACKET_BYTES` 同时是"尺寸"与"容量" | 必须拆分（§4.2） |
| **A6** | `RenderProductionFrame` 每帧按 aspect 重算相机；`PumpOneFrame` 按 FBO 设 viewport；`ReadPickPixel` 按 FBO 判越界 | **渲染与 pick 几何已尺寸无关**，E 不需改引擎渲染 |
| **A7** | `PresentationFrame.TryDecode` 要求 `length == 1,920,016` 且 `w/h == 800/600` | 动态尺寸必然失败 |
| **A8** | `MainWindow.RequestPickAtPoint` 传 `PresentationFrame.ExpectedWidth/Height`（常量） | 动态尺寸下 pick 坐标错位 |
| **A9** | `TryFetchPresentationFrame` **每帧** `new byte[1.92 MB]`（30 Hz ≈ 58 MB/s 垃圾） | 容量放大到 8.3 MB 后变成 ~250 MB/s ⇒ **E 必须改成复用缓冲**（否则扩容不可接受） |
| **A10** | `OpenGLFramebuffer::Resize` 静默 `max(w,1)`，且 **先 Destroy 再创建** | 零尺寸被伪装成 1×1；创建失败会**丢掉旧帧**（无法"保留旧 FBO 继续显示"） |
| **A11** | EngineHost **无** resize 通道；legacy 的 `EditorSession_SetViewportSize` 属禁止路径 | §7 的 ABI 决策 |
| **A12** | "无新帧" 判定基于 `m_FrameId == m_LastFetchedFrameId` | 若 resize 不推进 `frame_id`，UI 会以为没有新帧 ⇒ 尺寸变了也看不到 |
| **A13** | pick 的 readback 按 FBO 尺寸判定，但请求的坐标是**点击时**的帧空间 | 同帧 resize 会让 pending pick 的坐标失效（§8） |
| **A14** | `test_bridge:1762` 断言 `PresentationTargetWidth() == ENGINE_HOST_PRESENTATION_WIDTH` | 常量语义变化后必须改为"默认尺寸"断言 |

---

## 7. ABI 结论（**唯一的待批决策**）

### 7.1 必须新增一个 op，否则 E 无法实现

UI 与 native 之间**不存在**任何尺寸输入通道：

| 候选通道 | 判定 |
|---|---|
| 复用既有 op | 无任何 op 的语义是"设置渲染目标尺寸"（`SET_ENTITY_POSITION` 等都不合适） |
| 通过 `PumpOneFrame(dt)` 传 | 签名只有 dt，没有尺寸槽位 |
| 新 export `EngineHost_SetViewportSize` | 违反"不新增 ABI 导出"的既有偏好，且比 op 更宽 |
| **新增 append-only op** | **唯一合理选项** |

```text
ENGINE_HOST_OP_SET_VIEWPORT_SIZE = 30      // OP_COUNT 30 -> 31
参数：floatArg[0] = width, floatArg[1] = height
```

**不需要**新增：export、status、stage、packet 字段。

### 7.2 版本必须升到 v1.5

理由（与 P-1 同构）：`ENGINE_HOST_FRAME_PACKET_BYTES` 的**语义与数值**都要变
（从"帧大小"变成"最大容量"），这是**真实的契约变更**；
不升版本会让 v1.4 managed（按 1.92 MB 分配）与 v1.5 native（可能产出更大帧）
混搭成"运行时才失败"的组合，而不是在 `Create()` 处被 lockstep 抓住。

### 7.3 待批的完整变更面

| 项 | 变化 |
|---|---|
| ABI 版本 | v1.4 → **v1.5** |
| 新 export | **0** |
| 新 op | **1**（`SET_VIEWPORT_SIZE = 30`，`OP_COUNT` 30 → 31） |
| 新 status / stage | **0** |
| packet 字段 | **0**（header 已有 `w/h`） |
| 容量常量 | 拆分出"最大尺寸"，`FRAME_PACKET_BYTES` 语义变为最大容量 |
| 既有默认尺寸 | 仍 800×600（P1/P2 证据面不变） |

---

## 8. resize 生命周期（E-C6，目标模型）

```text
Avalonia SizeChanged（ViewportImage 实际布局尺寸）
    │ ① 去抖/合并：只记录"最新目标尺寸"，不排队（容量=1，last-wins）
    ▼
Dispatch(SET_VIEWPORT_SIZE, [w,h])
    │ ② 只登记，不改任何 GL 状态（Submit 在无 context 的 command loop 上执行）
    ▼
下一次 PumpOneFrame（context current 窗口内）
    │ ③ 在 Clear/Bind/Viewport **之前**应用：
    │      - 校验 w/h（>0、<= 最大尺寸、有限）
    │      - 校验失败 ⇒ 丢弃该请求 + 记诊断 + **保持旧 FBO**
    │      - 校验通过 ⇒ PresentationFbo->Resize(w,h)
    │      - 重建失败 ⇒ 帧不发布（fail-closed，不混用新旧）
    │      - **若有 pending pick ⇒ 丢弃该请求**（E-C7，坐标属旧帧空间）
    ▼
④ 渲染 + readback（都用**新** FBO 尺寸）
    ▼
⑤ ++m_FrameId（既有行为）⇒ 新尺寸的帧带有**新** frame identity
    ▼
⑥ Fetch：header 的 w/h 就是新尺寸；managed 按 header 解码并重建 bitmap
    ▼
⑦ 新帧呈现后，pick 才允许基于新尺寸发起
```

### 冻结条款

| 编号 | 条款 |
|---|---|
| **E-C6a** | **合并**：连续 resize 只保留最新目标尺寸（capacity=1，last-wins），不排队、不逐次重建 |
| **E-C6b** | **零尺寸 / 最小化**：`w<=0 或 h<=0` ⇒ **拒绝**（不 clamp 到 1）；host 保持上一个有效尺寸继续渲染。**E 不引入"暂停渲染"状态**（属独立性能 slice） |
| **E-C6c** | **尺寸上限**：超过最大尺寸 ⇒ 拒绝并记诊断，不截断（截断会改变画面内容而不报错） |
| **E-C6d** | **重建失败**：`Resize` 已 Destroy 旧资源 ⇒ 帧**不发布**；`m_ReadbackVerified=false`；Fetch 报 `NOT_READY`；UI 保留最后一张 bitmap 但**禁止 pick**，直到新有效帧到达 |
| **E-C6e** | **不混用**：尺寸切换只在 `PumpOneFrame` 的 current-context 窗口内、渲染**之前**发生 ⇒ 元数据与像素天然同帧一致，不存在"新尺寸 + 旧像素" |
| **E-C6f** | **frame identity**：沿用既有 `m_FrameId`，**不新增 generation 字段**。resize 的"新尺寸帧"天然是新 `frame_id`，因此 managed 的 `stale-frame` 判定继续有效 |
| **E-C6g** | **缓冲区**：managed 按最大容量分配**一次**并复用（不再每帧 `new`）；容量不足 ⇒ 拒绝解码并报错，**绝不越界读** |

---

## 9. 与 P-1 stale 语义的衔接（E-C7）

**不新造状态机**，复用既有三态：

| 规则 | 实现位置 |
|---|---|
| 帧 identity = `packet.frame_id` | 既有 |
| pick 结果必须属于当前呈现帧：`result.frame_id < presentedFrameId` ⇒ `stale-frame`（不应用） | 既有（`MainViewModel.TryConsumeViewportPick`） |
| 索引合法但来自旧帧 ⇒ 仍拒绝 | 既有规则 + 上面的 frame_id 闸 |
| **resize 使 pending pick 失效** | **新增**（native 侧）：应用 resize 的那一帧，若存在 pending pick，则丢弃并产出 `ENGINE_HOST_PICK_NOT_READY`（不新增 status） |
| FBO 重建失败后不得用旧帧坐标 pick | 同 E-C6d：Fetch 报 `NOT_READY`、managed 不发起 pick |

> 为什么 native 侧必须丢弃而不是依赖 managed：同帧 resize 产出的结果会带**新** `frame_id`，
> 因此 managed 的 `frame_id < presented` 判定**不会**拒绝它 —— 必须在 native 侧断掉。

---

## 10. 每个边界的失败处理与可执行断言（E Step 2 的 gate 计划）

| 边界 | 失败处理 | 可执行断言 |
|---|---|---|
| SizeChanged → Dispatch | 尺寸非法则不发送 | 记录"未发送"计数，gate 断言 0 次非法发送 |
| Dispatch → 应用 | 非 current-context、或校验失败 ⇒ 丢弃 | `GetViewportSize` 仍返回旧尺寸 |
| FBO Resize | 失败 ⇒ 不发布帧 | `ReadbackVerified == false`；Fetch 报 `NOT_READY` |
| Fetch 尺寸 | FBO w/h 与 header 一致 | 断言 header.w/h == `PresentationTargetWidth/Height` |
| Fetch 容量 | `capacity < 16 + w*h*4` ⇒ 拒绝且**不写字节** | 断言 buffer 全 0 |
| managed 解码 | `w/h` 越界或 payload 不足 ⇒ 拒绝 | 断言 `TryDecode == false` 且 `frame == null` |
| managed 分配 | 容量不足 ⇒ 明确错误 | 断言"保留旧 bitmap + 报错" |
| pick 坐标 | 尺寸未知 / 帧尺寸不符 ⇒ 不发起 | 断言 `RequestPickAtPoint` 返回 false 且无 pending |
| 同帧 resize + pick | native 丢弃请求 | 断言结果状态为 `NOT_READY` 且选中未变 |
| 旧帧 pick 结果 | `stale-frame` | 断言 `PickHitsApplied` 不增长 |

### 10.1 审计给出的验收场景映射

| 场景 | 预期 | 由哪些断言覆盖 |
|---|---|---|
| 800×600 → 640×480 | 帧尺寸/解码/映射一致 | header 断言 + 解码尺寸 + 映射 round-trip |
| 640×480 → 1280×720 | 缓冲区扩容无截断 | 容量断言 + `length >= 16+w*h*4` + 像素计数 |
| 控件小于帧且不缩放 | 可见区仍逐像素对应 | mapper 用 `displayed=(0,0,frameW,frameH)` + 控件 554×600 的既有实测 |
| 控件缩放显示帧 | 应用真实显示比例 | mapper 的 scale 断言（displayed≠frame） |
| 控件尺寸变零 | 不建非法 FBO、不发无效 pick | resize 被拒 + `GetViewportSize` 不变 + 无 pending |
| resize 与 pick 同帧 | 旧帧结果不改选择 | native 丢弃 + managed `stale-frame` 双断言 |
| FBO 重建失败 | 不混用新尺寸与旧内容 | `NOT_READY` + 帧未发布 |
| 帧尺寸不可信/容量不足 | 安全拒绝 | 解码拒绝 + 不写字节 |

---

## 11. E Step 2 的最小实现范围（待批后执行）

按依赖顺序，**只做这些**：

1. **ABI v1.5**：新增 `ENGINE_HOST_OP_SET_VIEWPORT_SIZE = 30`；拆分容量常量
   （默认 800×600 / 最大 1920×1080 / 容量 = max）；`static_assert` 保持 packet 布局不变。
2. **native**：`m_PendingViewportSize`（capacity=1, last-wins）+ `PumpOneFrame` 内应用
   （校验 → Resize → 丢弃 pending pick）；`FetchPresentationFrame` 的尺寸校验从
   **等值**改为**范围**（`0 < w,h <= max` 且 `capacity >= 16+w*h*4`）。
3. **managed**：`PresentationFrame` 按 header 解码；fetch 缓冲**复用且按最大容量**；
   `ExpectedWidth/Height` 降级为"默认尺寸"。
4. **View**：接 `SizeChanged` → 去抖 → `Dispatch(SET_VIEWPORT_SIZE)`；
   `RequestPickAtPoint` 改用**当前显示帧**的尺寸。
5. **mapper**：按 §5.3 扩参（新增 displayed rect），保留全部拒绝条件。
6. **gate**：`--viewportsize`，覆盖 §10.1 的 8 个场景 + §10 的每边界断言。

**明确不在 E 内**：暂停渲染状态机、缩放呈现模式（`Stretch` 仍为 `None`）、
gizmo/camera、命令行参数精确匹配、integrity gate 的 7 项既有失败。

---

## 12. 本 Step 的边界

- **零代码**：未修改任何 native / managed / shader / ABI / I2 / gate。
- 未修改 `assets/gp01/*`；未新增测试。
- ABI 仍 **v1.4**，`PICK=29`、`OP_COUNT=30`、packet 32B 均未变。
- 待批：§7 的 `SET_VIEWPORT_SIZE` op + v1.5 升版。
