# HRC-3 Step 2 Pre-flight — Presentation Reality v1：可行性判定（STOP at contract gap）

> **阶段**：UI-HRC-3 / Step 2 前置可行性审计（**零代码改动**）
> **日期**：2026-09-28
> **判定**：**STOP —— D1（UI 真正显示 runtime frame）在 D2（zero-new-ABI）边界内不可实现。**
> Step 2 不进入编码；缺口与所需前置 slice 记录于 §3 / §4。
> **前置**：`docs/Editor-Avalonia-HRC3-Viewport-Reality-Audit.md`（D1~D5 已 RATIFIED）
> **依据**：D1 RATIFIED（presentation reality）/ D2 RATIFIED（CPU readback → `WriteableBitmap`，**不新增 ABI**）/
> D3 RATIFIED as later slice / D4 RATIFIED，implementation deferred / D5 RATIFIED（Windows hidden-GLFW）

---

## 1. 判定摘要

D2 规定"**不新增 ABI**"，D1 要求"**Avalonia UI 展示的是当前 EngineHost runtime frame**"。
把这两条放在一起检验后，**现有冻结 ABI 无法把任何像素送达 managed**，且**生产路径根本没有
场景绘制**。因此 D1 在 D2 边界内**不可实现**。

按 Step 2 的既定指令（"如果现有 ABI 不足以提供这个证明，Step 2 应停在 contract gap"），
本 Step **不开工**，改为交付缺口报告。

---

## 2. 三个缺口（逐条带出处）

### GAP-A：生产路径没有场景绘制（且不能靠接回 legacy 解决）

| 事实 | 出处 |
|---|---|
| `Application::InternalRender()` 首行即 `if (m_Headless) return;` —— headless 模式下**渲染整体短路** | `engine/src/Core/Application.cpp:536-538` |
| EngineHost 正是 headless 模式（`InitializePersistentHiddenContext(64,64)`） | `bridge/src/EngineHost.cpp:300` |
| EngineHost 每帧唯一的绘制动作是 `m_OffscreenFbo->Clear(...)` + 单像素探针 | `EngineHost.cpp:1141-1166` |
| 现存的场景渲染入口是 `m_Viewport.OnUpdate(dt, true)`，其唯一调用者是 **legacy imgui editor** | `engine/src/Editor/EngineEditor.cpp:478`（`EngineEditor::OnUpdate`） |
| `Application`（headless 生产路径）**从不**调用 `ViewportPanel::OnUpdate` | 全仓搜索无该调用点 |

**推论**：唯一现存的渲染驱动是 legacy imgui editor 循环。把 `ViewportPanel::OnUpdate` 或
`InternalRender` 从 EngineHost 帧里调起来，正是 §5.1 明令禁止的"把 legacy 路径接回 EngineHost"。
→ GAP-A **无法在 Step 2 内合法关闭**；它需要一条**headless 可渲染的 Host 自有渲染路径**
（engine 行为变更），并必须与 legacy editor 解耦。

### GAP-B：冻结 ABI 没有任何像素通道

| 事实 | 出处 |
|---|---|
| 24 个 `EngineHost_*` 导出中**无**返回像素 / 纹理句柄 / present 的接口 | `bridge/include/editor_bridge/capi.h`（导出清单） |
| 27 个冻结 op 全为 int/bool/string 的读写，**无**任何"读帧像素"op | `ENGINE_HOST_OP_*`（`NONE`/`COUNT` 除外） |
| 唯一的通用字节通道是 `EngineHost_Fetch(..., outBuffer, capacity, ...)`，但它按 **op** 分派，op 集合已冻结 | `capi.h:561` |
| managed 侧现有取像素 API 全是 `LegacyOnly`（生产 backend 抛 `HostCapabilityNotAvailableException`） | `EditorHostService.cs:407-432` |

**推论**：要让 managed 拿到像素，只有四条路，全部需要**新 ABI 或更糟的旁路**：

| 方案 | 性质 | 判定 |
|---|---|---|
| 在 `EngineHost_Fetch` 上新增 1 个 op（返回 `[w][h][frame_id][RGBA]`） | **ABI 增量**（op 枚举是冻结项：27 op） | 最小的合法增量，但**违反 D2 字面** |
| 新增导出函数 | ABI 变更 | 更大 |
| native 写文件 / 共享内存，managed 去读 | **隐藏通道** | 拒绝：等于未申报的新传输，且破坏单一事实源 |
| 借用 legacy `EditorSession_GetViewportPixels` | 引入 legacy session | 拒绝：违反已冻结的 no-legacy-fallback / 单 Host 所有权 |
| 复用 `OP_SCRIPT_READ` 塞 base64 像素 | 滥用冻结 op 契约 | 拒绝 |

### GAP-C：frame identity 无法可执行地证明

`EngineHost_GetRuntimeProofState` 暴露 `frameCount` / `tickCount`，因此"帧号"这个量是存在的。
但要满足你要求的硬条件（**UI 显示的就是该 runtime frame**，且不得用 timestamp / bitmap 非空 /
clear-color 顶替），必须**在同一次调用里原子返回"像素 + 该像素所属的 frame_id"**。

现状：`GetRuntimeProofState` 只返回标量，没有任何调用把像素和 frame_id 绑定返回。
→ 即使解决了 GAP-B 的最小增量，若不把 `frame_id` 与像素**同调用返回**，identity 仍不可执行证明。
这应当写进未来的 ABI 设计要求，而不是 Step 2 里用间接证据凑。

---

## 3. 关闭 GAP 所需的最小前置 slice

| # | slice | 内容 | 是否 ABI | 前置依赖 |
|---|---|---|---|---|
| **P1** | headless 渲染路径 | 新增 **Host 自有**的 headless 渲染步：把场景画进 Host-owned presentation FBO（v1 固定尺寸），**不依赖 imgui editor、不接回 legacy `ViewportPanel` 驱动** | 否（engine 内部） | 需裁决渲染入口归属（`Application::InternalRender` 去 headless guard，还是 EngineHost 自有 render step） |
| **P2** | presentation ABI | 在现有 `EngineHost_Fetch` 通道上新增 **1 个 op**，一次调用原子返回 `[w][h][frame_id][RGBA]` | **是（最小增量）** | P1；需版本策略 + managed mirror + native 测试 + I2 重跑 |
| **P3** | managed 呈现 + gate | `--viewport` gate：断言 bitmap 被更新、`presented.frame_id == capture 时的 frameCount`、内容非 clear-color、UI 控件与 payload 尺寸一致 | 否 | P2 |
| **P4** | pick（D3，后置） | 接线 `PickingFBO`（EncodeID + ReadPixel），ID buffer 随帧清理 | 是 | 独立 ABI freeze + I2 重跑 |
| **P5** | resize（D4，后置） | resize 信号通道（UI → Host），FBO 跟随控件尺寸，attachment/viewport 同步 | 是 | 同上 |

**VR-1 的 resize 子项与 VR-3 的 pick 在 Step 2 一律不得标 CLOSED**（D3/D4 已裁决后置）；
它们留在 P4/P5，不得用"不测"来换绿。

---

## 4. 需要裁决的冲突：D2 的"no new ABI"与 D1 不相容

D2 RATIFIED 的字面是"不新增 ABI"，但 GAP-B 证明：**在冻结 ABI 内不存在任何像素通道**。
两者不能同时成立。三条出路：

| 选项 | 内容 | 代价 |
|---|---|---|
| **A（建议）** | 把 D2 的"no new ABI"**收窄**为"**不新增导出函数**"，允许在既有 `EngineHost_Fetch` 通道上新增 **1 个 op**，且必须原子返回 `frame_id` + 像素 | 需一次最小 ABI 增量（版本策略 + mirror + native 测试 + I2 重跑）；仍远小于新增导出 |
| **B** | 严守 D2 字面：Step 2 **只交付本缺口报告**，D1 另立 HRC-3 presentation slice（同样含 P1/P2） | Step 2 无代码产出；D1 延后，但边界最干净 |
| **C** | 用文件 / 共享内存等旁路传像素 | **不建议**：未申报的新传输，破坏单一事实源，且无法给出可审计的 ABI 证据 |

**未获裁决前不启动 P1/P2 编码。** A8 保持完全独立、Deferred；HRC-2 的
pump / ownership / thread boundary / no-legacy-fallback 保持冻结。

---

## 5. 判定汇总

```text
Step 2 pre-flight:     COMPLETE（零代码）
verdict:               STOP at contract gap（D1 在 D2 边界内不可实现）
GAP-A (render):        生产路径无场景绘制；唯一现存渲染驱动属 legacy imgui editor（禁止接回）
GAP-B (transport):     冻结 ABI（24 导出 / 27 op）无任何像素通道
GAP-C (identity):      frame_id 存在但无"像素+frame_id 同调用"返回，identity 不可执行证明
required slices:       P1 headless 渲染（无 ABI）→ P2 最小 ABI 增量 → P3 managed gate
                        P4 pick（D3 后置）/ P5 resize（D4 后置），均含独立 ABI freeze
open decision:         D2 的 no-new-ABI 与 D1 冲突 → 选项 A / B / C（建议 A）
untouched:             HRC-2 契约、legacy 路径、SupportsViewport 语义、A8
```
