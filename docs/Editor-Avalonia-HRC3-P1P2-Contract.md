# HRC-3 — 修订 D2 + P1/P2 Mini-Contract 与 ABI Freeze

> **阶段**：UI-HRC-3 / P1+P2 契约冻结（**零代码改动**；写死后才允许动 native）
> **日期**：2026-09-28
> **状态**：`CONTRACT FROZEN`（D2 已修订并 RATIFIED；P1/P2 mini-contract 冻结；
> 剩 1 条 P2 子决策 D2.1 待 ratify，见 §5）
> **前置**：`Editor-Avalonia-HRC3-Viewport-Reality-Audit.md`（Step 1 CLOSED）·
> `Editor-Avalonia-HRC3-Step2-Preflight-Gap.md`（Step 2 PRE-FLIGHT GAP CLOSED）
> **路线**：Step 1 CLOSED → Step 2 Pre-flight GAP CLOSED → **P1** → **P2** → P3 →
> D3 pick slice → D4 resize slice
> **保持冻结**：HRC-2 的 single pump / Host ownership / thread boundary / no-legacy-fallback；
> legacy 路径；`SupportsViewport` 语义；A8（完全独立、Deferred）

---

## 1. 决策日志（D1~D5 最终）

| 决策 | 裁决 | 状态 |
|---|---|---|
| **D1** | HRC-3 目标 = **UI 真正显示当前 EngineHost runtime frame**（presentation reality），不是再证明一次 FBO/readback plumbing | RATIFIED |
| **D2（修订）** | **no new export function**；允许在既有 `EngineHost_Fetch` 通道上新增 **1 个 additive op**，且必须**原子返回 frame identity + pixels**。**这是 ABI increment，不得描述为"ABI 没变"** | **RATIFIED（修订）** |
| **D3** | pick（接线 `PickingFBO`）= **后置独立 slice**，需自己的 ABI freeze + native evidence + I2 重跑 | RATIFIED as later slice |
| **D4** | resize 语义批准（FBO 跟随 UI 尺寸、下一帧起 attachment/viewport 一致、不得破坏 pump 契约）；**实现后置**。VR-1 resize 子项在 P1/P2 **不得标 CLOSED** | RATIFIED，implementation deferred |
| **D5** | 证据环境 = **Windows hidden-GLFW 单机**（与 HRC-2 同范围）；**不得产生任何跨平台呈现暗示** | RATIFIED |
| 选项 A/B/C | **A RATIFIED**（Fetch 上一个 additive op）；**B 不采用**；**C 明确禁止**（file/shared-memory 旁路） | RATIFIED |

---

## 2. ABI / version 增量规则（冻结）

### 2.1 现状机制（已核实）

| 事实 | 出处 |
|---|---|
| 版本宏 `#define ENGINE_HOST_ABI_VERSION ((1 << 16) \| 1)`，注释为 **v1.1**（高 16 位 = major，低 16 位 = minor） | `capi.h:370` |
| 单一来源：native `kEngineHostAbiVersion`（`EngineHost.cpp:30`）+ `EngineHost_GetApiVersion`（`capi.cpp:787`）+ managed `EditorBridgeApi.EngineHostExpectedVersion` | 三处共同引用同一宏 |
| managed 在 bootstrap 时**严格相等校验**，不匹配即抛 `HostOperationException(InitFailed/Bootstrap, "EngineHost ABI version mismatch: got 0x… expected 0x…")` | `EditorHostService.cs:548-553` |
| `RuntimeResult` 布局 **76B**，且已有测试断言 `sizeof(RuntimeResult) == 76` | `test_editor_capi.cpp`（`ResultLayout`） |
| op 合法性判定为区间式 `op > ENGINE_HOST_OP_NONE && op < ENGINE_HOST_OP_COUNT` | `EngineHost.cpp:1629-1631` |

### 2.2 增量规则（本次及后续）

1. **additive op = minor bump**：新增 fetch/present 类 op → `ENGINE_HOST_ABI_VERSION` 改为
   `((1 << 16) | 2)`，managed `EngineHostExpectedVersion` **同批次**更新。
   两侧**必须同 commit 变更**（严格相等校验会立刻抓出漏改的一侧）。
2. **禁止扩展 `RuntimeResult`**：不得新增字段、不得改 76B 布局。frame identity 等新增信息
   一律放进**新 op 自己的 packet**，不进 `RuntimeResult`。
3. **禁止新增导出函数**（D2 修订）：能力只能挂在既有 24 个 `EngineHost_*` 导出上
   （本次为 `EngineHost_Fetch`）。
4. **禁止改变既有 op 的语义/容量/返回约定**（见 §2.3）。新增 op 的行为不得影响 27 个既有 op。
5. **major bump 的判据**（未来触发条件，本次不触发）：删除/改语义既有 op、改变 `RuntimeResult`
   布局、改变既有导出签名或线程归属、改变 capacity 协议。
6. **historical baseline 保留**：HRC-2 冻结的 **27 concrete ops @ v1.1** 作为
   **historical baseline 记录在案，不被新 op 覆盖**；新 op 使 `OP_COUNT` 变为 28。
   任何文档/测试不得再把"27 op"写成当前值，必须写成"27 op @ v1.1（historical）+ 1 additive @ v1.2"。

### 2.3 既有 Fetch 约定（P2 不得破坏）

| 约定 | 出处 |
|---|---|
| 未知 op 或 `FetchCapacityForOp(op) == 0` → `INVALID_STATE / STAGE_EDIT`，返回 -1 | `EngineHost.cpp:1917-1925` |
| **容量模型是"每 op 静态 required size"**（4096 / 512 / 12），`capacity < required` → `INVALID_STATE / STAGE_EDIT`（"output buffer is smaller than the op requires"），返回 -1。**当前没有 size-query 两阶段协议** | `EngineHost.cpp:1637-1655`、`1921-1927` |
| 成功：`code >= 0` → `SetResult(SUCCESS, STAGE_EDIT, …)`；fetch 走 `Submit(...)` 进 runtime 命令队列 | `EngineHost.cpp:1928-1948` |
| fetch op 需要 session（`RequireSessionForEdit`） | `EngineHost.cpp:1930` |
| 需要 GL context 的 op 由 `RequiresGraphicsContext` 标注（当前仅 `ASSIGN_SPRITE`） | `EngineHost.cpp:1633-1635` |

---

## 3. P1 Mini-Contract — Production Scene Render Path（native-only，无 ABI）

### 3.1 目标

让**生产路径**（EngineHost / headless）**真的把场景画进 Host-owned render target**，
关闭 GAP-A。当前生产帧只做 `m_OffscreenFbo->Clear(...)` + 单像素探针
（`EngineHost.cpp:1141-1166`），且 `Application::InternalRender()` 在 headless 下直接 return
（`Application.cpp:538`）。

### 3.2 硬条件（违反即 FAIL）

| # | 条件 |
|---|---|
| **P1-H1** | **禁止**从 EngineHost 调用 `ViewportPanel::OnUpdate()`。全仓当前唯一调用点是 legacy imgui editor（`EngineEditor.cpp:478`），接回它 = 违反 no-legacy-fallback |
| **P1-H2** | **禁止**让生产渲染路径重新依赖 legacy `ViewportPanel`（含其 ImGui 绘制、窗口/FBO 假设、readback 缓冲） |
| **P1-H3** | 必须形成 **production-owned scene render entry**，其 **ownership 属于 production runtime**（EngineHost / headless Application），由 **既有 EngineHost frame/pump 驱动**；**不得**新增第二个 tick 源、旁路 pump 或 UI 侧渲染调用 |
| **P1-H4** | **不得**修改 HRC-2 冻结项：single pump / Host ownership / thread boundary / no-legacy-fallback / `SupportsViewport` 语义 |
| **P1-H5** | 不得把 clear-color 或任何静态/合成图案当作"场景帧"（P3 判定会直接抓到） |
| **P1-H6** | v1 render target **固定尺寸**（D4 已后置）；尺寸常量必须在 contract 与代码中**同名同值**，供 P2 静态容量与 P3 断言共用 |
| **P1-H7** | 渲染失败必须走既有 typed 失败通道（`status`/`stage`/`diagnostic`），不得静默吞掉；建议 stage 复用 `ENGINE_HOST_STAGE_FBO`，GL 错误用 `ENGINE_HOST_STATUS_GL_FAILED`（与既有 `EDITOR_BRIDGE_FAIL_FBO` 注入语义一致） |

### 3.3 待冻结的实现选择（需在 P1 开工前明确，不在本文档预设）

`Application::InternalRender()` 的 headless 短路如何解决，属实现归属问题，两条候选都**不**触碰
legacy editor：

- **P1-a**：让 `Application` 拥有 headless 可用的渲染步（去掉/收窄 headless guard），
  EngineHost 在既有帧内调用它；render entry 仍归 `Application`（production-owned）。
- **P1-b**：EngineHost 自有 render step（自行 make-current → 渲染 → 记录 frame_id），
  不改 `Application`。

两者都必须满足 P1-H1~H7。**建议 P1-a**（渲染所有权留在 engine 侧 `Application`，EngineHost 只编排），
但需你确认；本 slice 不预设结论。

### 3.4 证据要求（P1 自身，不含 managed 呈现）

- native contract test：生产帧后 render target **不是**仅 clear —— 至少能证明
  "渲染步执行过且产出了与 clear 不同的像素"（可复用 `OpenGLFramebuffer::ReadColorPixel` 探针，
  但断言对象必须是**场景内容**而非 clear color）。
- 不破坏 `test_hrc` 15/15、`test_bridge` 41/41、`test_scripting` 27/27（Debug + Release）。
- 不改变 `I2`（本 slice 无新测试则 in-scope 仍 240；新增 native 测试则按 §5 复验规则更新）。

---

## 4. P2 Mini-Contract — Frame Presentation Fetch（1 个 additive op）

### 4.1 目标

在既有 `EngineHost_Fetch` 上新增 **1 个** op，一次调用**原子**返回
`{width, height, frame_id, RGBA payload}`，使 P3 能**可执行地**证明"UI 显示的就是该 frame"。
关闭 GAP-B + GAP-C。

### 4.2 硬条件

| # | 条件 |
|---|---|
| **P2-H1** | **不新增导出函数**；只新增 op（`OP_COUNT` 27→28，version → `((1<<16)｜2)`，managed 期望值同批更新） |
| **P2-H2** | **pixels 与其 owning `frame_id` 必须由同一次 native operation 返回**，且 `frame_id` 必须在**与 readback 同一临界区内**捕获（既有 `m_FrameId` 原子量，`EngineHost.cpp:1175` 自增） |
| **P2-H3** | **禁止**复用 `RuntimeResult` 承载新增字段（保持 76B）；packet 自带 `frame_id` |
| **P2-H4** | 读回必须**在 runtime 线程执行**（`Fetch` 走 `Submit(...)` 命令队列，`EngineHost.cpp:1928`），**禁止**在 UI 线程做 GL 访问 |
| **P2-H5** | 该 op 必须加入 `RequiresGraphicsContext`（FBO readback 需要 current context） |
| **P2-H6** | **禁止**用三种伪证代替 frame identity：bitmap 非空 / 颜色不同于 clear color / timestamp 或"最近一次 frame"推断 |
| **P2-H7** | **陈旧 packet 必须显式状态**：若自上次 fetch 后没有新帧被渲染，返回明确的"无新帧"结果（**不得**静默重发上一帧像素）。建议新增 `ENGINE_HOST_STATUS_NO_NEW_FRAME`（status 枚举 additive，同批更新 managed mirror） |
| **P2-H8** | 不得改变既有 27 个 op 的容量/返回/线程语义（§2.3） |
| **P2-H9** | 失败必须 typed：`status`/`stage`/`diagnostic` 三者齐全（stage 建议 `ENGINE_HOST_STAGE_FBO`） |

### 4.3 必须补齐的 ABI 交付物（你已明确要求）

1. **native op 实现 + managed mirror**（`EditorBridgeApi` 常量、`EditorHostService` 包装）
2. **native contract test**：
   - packet 自洽（`width`/`height` 与 payload 长度一致）
   - `frame_id` 单调且与"产生该像素的帧"一致
   - 无新帧时返回 `NO_NEW_FRAME` 而非陈旧像素
   - capacity 不足时行为符合 §5 D2.1 裁决
3. **既有 ABI invariants 全部复验**：`RuntimeResult` 仍 76B、`GetApiVersion` 与 managed 期望一致、
   27 个既有 op 行为不变（`OpDispatchRoutesEverySessionCall` 等既有测试全绿）
4. **I2 重跑到 240**（含新增 native 测试时同步更新 baseline 记录，不得静默变动）
5. **HRC-2 冻结面不动**：pump / ownership / thread boundary / no-legacy-fallback
6. `docs/Editor-Avalonia-HRC2-Closure.md` 的 ABI 冻结条目按 §2.2 规则 6 更新表述
   （"27 op @ v1.1 historical + 1 additive @ v1.2"），**不重开 HRC-2 判定**

---

## 5. 待 ratify 的 P2 子决策 D2.1：packet 尺寸协议

现有 Fetch 的容量模型是**每 op 静态 required size**，且**没有** size-query 两阶段协议
（§2.3）。而 frame packet 大小依赖 `width*height*4`。三条：

| 选项 | 内容 | 代价 / 影响 |
|---|---|---|
| **D2.1-a（建议）** | v1 目标**固定尺寸**（P1-H6），packet 大小确定 → 走既有**静态容量**模型，只需给新 op 加一条 `FetchCapacityForOp` case，**不改变任何既有协议** | 内存按最大尺寸常驻；D4（resize）落地时**必须**升级为两阶段 size-query，那属于 D4 slice 的 ABI 增量 |
| **D2.1-b** | 直接实现两阶段 size-query（`outBuffer=nullptr, capacity=0` 返回 required size） | 需要**改变既有 `EngineHost_Fetch` 的容量语义**（今天 capacity 0 = 报错）→ 触碰既有 op 语义，违反 P2-H8 精神；范围更大 |
| **D2.1-c** | 固定上限 + 运行时截断/报错 | 会出现"present 尺寸与 UI 不符"的静默风险，不建议 |

**建议 D2.1-a**：与 D4（resize 后置）一致，把两阶段协议留给 D4 slice，届时作为独立 ABI 增量
（连带 P2-H8 的例外需要单独立项）。

---

## 6. 判定汇总

```text
D1 presentation reality:            RATIFIED
D2 (revised) additive Fetch op:     RATIFIED —— 明确是 ABI increment（v1.1 -> v1.2）
D3 pick:                            RATIFIED as later slice（独立 ABI freeze + I2）
D4 resize:                          RATIFIED，implementation deferred（VR-1 resize 不得标 CLOSED）
D5 evidence env:                    RATIFIED（Windows hidden-GLFW；A8 独立）
option C (side channel):            明确禁止
P1 mini-contract:                   FROZEN（P1-H1~H7；实现归属 P1-a/P1-b 待确认）
P2 mini-contract:                   FROZEN（P2-H1~H9 + 六项 ABI 交付物）
ABI/version increment rules:         FROZEN（§2.2 六条，含 historical baseline 保留规则）
open sub-decision:                  D2.1（packet 尺寸协议）→ 建议 D2.1-a
untouched:                          HRC-2 冻结面、legacy 路径、SupportsViewport、A8
code written this step:             0
```
