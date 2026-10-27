# HRC-3 D3 Pick Reality Audit + Contract Freeze

> **阶段**：UI-HRC-3 / **D3 Pick**（Step 1：Audit + Contract Freeze，**零代码**）
> **日期**：2026-09-29
> **状态**：`D3 STEP-1 CLOSED`（D3-C1~D3-C8 已冻结；**D3 Step 2 尚未批准**）
> **前置**：P1 CLOSED · P2 CLOSED · P2 已入 standing regression surface · ABI v1.2
> **仍未开始 / 自动延续**：D4 Resize（独立）· A8

---

## 1. 结论摘要（先说最重要的）

1. **`D3` 不是"接线"，是"新建一条从无到有的路径"。** 树内存在 **4 套** pick 实现，
   **全部 0 生产引用**。没有任何一条被 production 路径使用。
2. **没有任何一处把 pick 结果与 frame 绑定。** 三条 readback 实现读的都是
   "当前 attachment 里的内容"，即被明令禁止的**「最近一次 pick buffer」**语义。
3. **ID 格式在树内四份声明互不一致**，且 **sentinel 有 0 与 -1 两种并存的活冲突**。
4. **坐标 y-flip 在树内有两种不同实现**（`height-1-y` 与 `height-y`），相差一行像素。
5. 因此 D3 的第一步必须是**冻结 contract**，而不是"把现成的 `PickingFBO` 接上"。

---

## 2. Audit：现有实现的真实状态

| # | 事实 | 证据 |
|---|---|---|
| A1 | `PickingFBO` 实现完整（GL 独立 FBO、R32UI、`EncodeID` + `ReadPixel`），但 **0 外部引用** | `engine/src/Editor/PickingFBO.cpp`；全树引用数 = 0 |
| A2 | `OpenGLFramebuffer` 内含**第二套**：MRT `GL_COLOR_ATTACHMENT1`（`m_PickTexture`, R32I）+ `BindForPickRendering()` + `ReadPixelID()`，**0 调用方** | `OpenGLFramebuffer.cpp:131,209,220`；仅头文件文档注释引用 |
| A3 | `IDBufferPass` 是**第三套**，且**不在 GL 上** —— 它走 `RHI::IRHIDevice` + CPU readback buffer，**0 外部引用** | `engine/src/Rendering/IDBufferPass.cpp:32,43,58,117` |
| A4 | shader 侧**确实**会写 entity ID：`out int EntityID` 到 `location = 1`，见 `pick.frag` / `simple.frag` / `billboard.frag` | `assets/shaders/pick.frag:4,9`、`simple.frag:5,100`、`billboard.frag:3,14` |
| A5 | 但 **C++ 从不设置 `u_EntityID`，也从不加载 `pick.frag`** —— 声明存在，**无一条生产 render 路径真正写入 ID** | `*.cpp/*.h` 中 `pick.frag` / `u_EntityID` 命中数 = 0 |
| A6 | legacy pick 是**另一件事**：`EditorSession::ViewportPick` 是 **stub，恒返回 -1** + `TODO Phase 7-B`；真实读取在 `ViewportPanel::HandleMousePicking`（读**自己的 ImGui FBO**），属 **NHI-1 禁止路径** | `bridge/src/EditorSession.cpp:1105`；`engine/src/Editor/ViewportPanel.cpp:390`；`capi.cpp:639` |
| A7 | **无 frame 绑定**：`PickingFBO::ReadPixel` 与 `OpenGLFramebuffer::ReadPixelID` 都不携带、不校验任何 `frame_id` | 两处实现体均无 frame 字段 |
| A8 | **线程守卫不一致**：`ReadPixelID` 有 `IsOwnerThread() && glfwGetCurrentContext()`；**`PickingFBO::Bind/Unbind/ReadPixel/Resize` 完全没有** | `OpenGLFramebuffer.cpp:221-222` vs `PickingFBO.cpp:108-153` |
| A9 | **ID 格式四份声明不一致**：shader=`out int`（有符号）· `OpenGLFramebuffer`=R32I/GL_INT · `PickingFBO`=R32UI/GL_UNSIGNED_INT · `IDBufferPass`=R32_UInt | 见 A1/A2/A3/A4 |
| A10 | **sentinel 活冲突**：`IDBufferPass.h:72` 与 `EngineEditor.cpp:214` 用 **0**；`lit.frag:10` 的 `u_EntityID` **默认 -1**（会写入 0xFFFFFFFF）；legacy `ViewportPick` 返回 **-1** | 见各处 |
| A11 | **y-flip 两种实现**：`PickingFBO` 用 `height - 1 - y`；`ViewportPanel` 用 `height - y`（差一行） | `PickingFBO.cpp:140` vs `ViewportPanel.cpp:398` |
| A12 | **越界处理不一致**：`PickingFBO` **clamp**（越界静默变边缘像素=假命中）；`ReadPixelID` **拒绝**（返回 0，与 no-hit 混同） | `PickingFBO.cpp:143-144` vs `OpenGLFramebuffer.cpp:227-230` |
| A13 | **readback 失败与 no-hit 混同**：`ReadPixelID` 的所有早退（无纹理/非 owner thread/无 context）都返回 **0**，与"背景命中 0"无法区分 | `OpenGLFramebuffer.cpp:221-231` |
| A14 | `location = 1` 约定**冲突**：deferred 路径用它输出颜色 `out uint g_NormalRgh (RGB10A2)`，pick 路径要用它输出整数 ID | `deferred_geom.frag:20` vs `pick.frag:4` |
| A15 | entity identity 现约定是 **`GameObject::ID`**（`scene->FindByID`），**不是** ECS `EntityID` | `EngineEditor.cpp:216` |
| A16 | legacy 隐含 **16-bit 上限**（`if (eid > 65535) eid = -1;`），与 32-bit attachment 不一致 | `ViewportPanel.cpp:409` |

---

## 3. D3-C1 ~ D3-C8 Contract Freeze

### D3-C1 — pick target ownership

- `Pick(x,y)` 的唯一真值是**当前 presented/runtime frame N 的 ID 表面**。
- ID 表面必须与 P2 的 presentation frame **在同一次 render 内产生**（同一 command stream、同一 frame 边界）。
- ownership 属于 **runtime frame**，不属于 UI，也不属于任何 FBO 对象的生命周期。
- 明确**不是**：legacy editor 的即时状态、VM 状态、"鼠标点击后查当前 VM"。

### D3-C2 — ID encoding / background sentinel

- **单一 sentinel**：`0` 恒表示 background / no-hit。`0` 是**正常结果，不是错误**。
- **entity 编码必须 ≥ 1**（1-based），否则 entity 0 与 background 碰撞。
- 冻结 **`out int` + R32I / `GL_INT` / `GL_RED_INTEGER`**。理由：**已有 4 个 shader 与 `OpenGLFramebuffer` 现成 attachment 在这一点上一致**；`PickingFBO`(R32UI) 与 `IDBufferPass`(R32_UInt) 是少数派，Step 2 不得反向统一到无符号。
- **禁止** `PickingFBO::EncodeID` 的 RGB 24-bit 编码（丢高 8 bit，且与单通道 attachment 不兼容）。
- **必须**在 Step 2 处理 A10：`lit.frag` 的 `u_EntityID = -1` 默认值会让未设置 material 写入 0xFFFFFFFF，与 sentinel 0 冲突。
- **不继承** A16 的 65535 上限；若确需上限必须显式声明，**不得静默 clamp 成 -1**。

### D3-C3 — coordinate transform / origin

- 冻结 **top-left 原点**（UI / Avalonia 语义），readback 时**单次** y-flip：`py = height - 1 - y`（采 A11 的 `PickingFBO` 写法，修正 `ViewportPanel` 的差一行）。
- D3 期间**固定 800×600，与 presentation frame 1:1**。不引入 `m_ResolutionScale`，不引入 ImGui `viewportMinX/MinY`。
- **越界必须显式拒绝**（`OutsideViewport`），**禁止 clamp**（A12：clamp 会把越界点击静默变成边缘假命中）。
- 非法坐标（非有限值）→ `InvalidCoordinate`。

### D3-C4 — frame identity（继承 P2 证据标准）

- pick result **必须携带 `pickFrameId`**，且可证明属于当前 presented frame N。
- **stale 结果（`pickFrameId < presentedFrameId`）必须被显式区分**，返回 `StaleFrame` + 携带其 `pickFrameId`；**不得**静默当作当前帧结果。
- **禁止**"最近一次 pick buffer"语义（A7）。
- UI event 快于 runtime frame 时：返回**上一次已 presented frame** 的 ID 并**标明该 frame**，
  **不猜测、不排队等待、不阻塞 UI**。

### D3-C5 — runtime thread / pump integration

- 复用 P2 的**唯一 pump**（`OnProductionFrameTick` / `RuntimeTick`）；ID 渲染发生在该 frame 内。
- pick 在 **runtime thread** 执行；**UI thread 不做 GL**。
- **不新建 pick thread**，**不建 UI-side GL path**，**不加 legacy fallback**。
- 不得重新打开 HRC-2。
- 约束：`PickingFBO` 现**无** owner-thread / current-context 守卫（A8），**直接使用会重开 B2(a)/B2(c) 同类缺陷** → 若 Step 2 选它，必须先补守卫。

### D3-C6 — result + failure semantics

**no-hit ≠ failure**；且 **readback 失败 ≠ no-hit**（A13 必须被分离）。

| 情形 | 语义 | 是否失败 |
|---|---|---|
| 背景 / 无实体 | `id = 0` + ok | **否** |
| 非法坐标（非有限值） | `InvalidCoordinate` | 是 |
| 越界 | `OutsideViewport`（显式拒绝） | 是 |
| 尚无任何 presented frame | `NoCurrentFrame` | 是 |
| disposed / not ready | `NotReady` | 是 |
| entity 在 frame 与结果之间被删除 | 返回命中 ID + 显式 `StaleEntity` 标记 | 否（但必须可辨） |
| stale frame 结果 | `StaleFrame` + 携带 `pickFrameId` | 是（不可当当前帧用） |

### D3-C7 — legacy path remains forbidden

- `ViewportPanel::HandleMousePicking` / `EditorSession_ViewportPick` 属 **NHI-1 禁止路径**，
  **不得**作为 D3 的实现，**也不得**作为 fallback。
- `EditorSession::ViewportPick` 的 `-1` stub **不得**被"接上"当 D3 结果。
- 不新增任何 legacy interop；不复活 `PickingFBO` 之外的 legacy FBO 读取。

### D3-C8 — D4 resize remains independent

- D3 冻结在**固定 800×600**。
- **不**因为 P2 已证明 800×600 成立就顺手接 resize。
- 动态尺寸 + FBO attachment resize 仍归 **D4**；`PickingFBO::Resize` 现有实现在 D4 之前**不得**进入生产。
- D3 不引入 `m_ResolutionScale`。

---

## 4. Step 1 Closure 结论与未决项

**8 条 contract 全部冻结（D3-C1 ~ D3-C8）。**

Step 2 **尚未开始、未获批准**。进入 Step 2 前必须先解决这些 Step 1 **没有**回答的问题：

| 未决 | 说明 |
|---|---|
| U1 | 在 4 套实现中**选哪一套**（本文件只冻结 contract，不做选型） |
| U2 | 谁来**真正设置** `u_EntityID` 并驱动 ID render pass —— A5 表明**当前无任何生产路径写入 ID**，这是新增工作量，**不是接线** |
| U3 | A14 的 `location = 1` 冲突如何隔离（deferred 颜色 vs pick 整数） |
| U4 | A10 的 `lit.frag u_EntityID = -1` 默认值如何修正 |
| U5 | entity identity 沿用 `GameObject::ID`（A15）还是引入独立 pick handle |

**本 Step 为零代码**：未修改任何 runtime / native / ABI / I2。
