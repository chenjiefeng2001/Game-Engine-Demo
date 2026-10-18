# HRC-3 D3 ABI Mini-Contract — Pick Request / Result Transport

> **阶段**：UI-HRC-3 / **D3 Pick**（ABI mini-contract，**docs-only，零代码**）
> **日期**：2026-09-29
> **状态**：`D3 ABI CONTRACT FROZEN`（transport 形状已冻结；**实现未批准**）
> **前置**：D3 Step 1 / Step 2 / Step 3 / Step 3.1 CLOSED · ABI 当前 **v1.2**
> **仍未开始 / 自动延续**：D3 transport 实现 · managed/UI 消费 · D4 Resize · A8

---

## 1. 现在真正缺的是什么

Step 3.1 已经用可执行证据证明 **native 语义正确**（known pixel → 精确 ID、
背景 → 精确 `-1`、坐标公式、无 clamp、`NotReady ≠ NoHit`）。

缺的是**链路**：

> UI / managed → runtime-thread pick request → typed pick result

本文件只冻结 **transport 形状**。不写代码，不改 ABI。

---

## 2. 决定性既有事实：GL op 天然是**异步**的

`capi.h:586-589` 对既有 A7 seam 的规定：

> GL op = pending edit id（>0），且 `result.status = ENGINE_HOST_STATUS_EDIT_ACCEPTED`
> —— **仅表示"已受理"，不表示已执行**。

执行发生在**下一次 `PumpOneFrame` 的 current-context 窗口内**。

因此：**UI 线程不可能同步拿到 pick 结果**。pick 需要 GL context、必须在 runtime 帧内
执行，request 与 result 天然**跨帧**。这一点决定了下面几乎所有答案。

---

## 3. 冻结的 transport 形状

```text
PickRequest
  x                    canonical 视口坐标（左上原点，见 Step 2 D3-C3）
  y
  request_id           由 pending edit id 回填（见 §6）

PickResult
  frame_id             **必填**：产生该结果的那一帧（D3-C4）
  request_id           回显，用于关联
  status               Hit | NoHit | OutsideViewport | InvalidCoordinate
                       | NotReady | Fault
  entity_id            仅 status == Hit 时有效，且 >= 1（D3-C2）
```

### 3.1 两段式（复制 P2 的既有纪律）

```text
UI thread                     runtime thread
   │
   ├─ EngineHost_Dispatch(op = PICK, floatArg = [x, y])
   │     → 返回 pending edit id，status = EDIT_ACCEPTED
   │     （仅表示已受理）
   │
   ├─ EngineHost_PumpOneFrame(...)
   │     └─ production render 内执行 pick pass，渲染 ID 到 attachment 1
   │        并把结果与该帧的 frame_id 绑定
   │
   └─ EngineHost_Fetch(op = PICK_RESULT, out = PickResultPacket)
         → 取回 typed result（含 frame_id / request_id / status / entity_id）
```

与 P2 完全同构：**request → pump → fetch**。不引入第二套机制。

---

## 4. 逐条回答

### Q1. 一个 request 对应哪个 runtime frame？

**对应"服务它的那一帧"**，且 result 必须携带该帧的 `frame_id`。

- pick pass 在该帧的 production render 内执行，ID 与颜色**同帧**产生（D3-C1）。
- **不**允许指定"回溯某一历史帧"：历史帧的 ID 表面已被覆盖。
- **不**允许跨帧等待：UI 线程不阻塞、不猜测（D3-C4）。
- 若 request 到达时本帧已过渲染点，则由**下一帧**服务，并在 result 里如实标明那一帧。

### Q2. 同 frame 多个 request 是否允许？

**pending 槽容量 = 1**。在已有 pending 未被 fetch 取走时再次请求：

- 返回**明确的 busy 状态**（新增 `PICK_BUSY` 类语义），**不静默覆盖**。
- 理由：静默覆盖会让"这个 result 属于哪个 request"变得不可判定，
  与 D3-C4「必须能证明属于哪一帧/哪次请求」直接冲突。
- 同一帧内的多次点击由调用方**串行化**（发一个、取走、再发下一个）。
  这符合 UI 现实：一次交互一个坐标。

### Q3. 是否需要 request id？

**需要。** 不是为了"完整性"，而是因为 §2 的异步 seam 让 result **跨帧**返回。
没有回显 id 就无法把 result 关联回具体那次点击。

- 复用**既有** pending edit id 机制，**不新增字段**。
- `RuntimeResult.operation_id`（已存在）承载 request 侧 id；
  result packet 回显它。

### Q4. `NoHit` 与 `OutsideViewport` 是否结构化区分？

**必须区分。** 语义根本不同：

| 状态 | 含义 | 是否失败 |
|---|---|---|
| `Hit` | 命中，`entity_id >= 1` | 否 |
| `NoHit` | 查询有效，该像素没有实体（sentinel `-1`） | **否** |
| `OutsideViewport` | 查询**无效**：坐标越界，已显式拒绝 | 是 |
| `InvalidCoordinate` | 查询**无效**：坐标非有限值 | 是 |
| `NotReady` | 无 pick 纹理 / 非 owner thread / 无 current context | 是 |
| `Fault` | host 进入 FAULTED / teardown 中 | 是 |

把它们合并成一个"没选中"就是把 D3-C6 已经拆开的东西重新糊在一起 —— 明确禁止。

### Q5. pick result 是否必须携带 `frame_id`？

**必须。** 且**不依赖**调用方去读 `RuntimeResult.frame_id` ——
typed pick packet 自身必须带 `frame_id`，使"frame 绑定"无法被忘记。

### Q6. 复用 `EngineHost_Fetch`，还是新增 additive `Dispatch` op？

**两者都用，但都是既有 seam 的 additive 扩展**：

- request 走既有 `EngineHost_Dispatch`（GL op，异步受理）——**不新增函数族**。
- result 走既有 `EngineHost_Fetch`（带输出缓冲的查询）——**不新增函数族**。

即：**只新增 op 编号与 result 状态码，不新增 transport 机制**。
这是本 contract 相对"新函数 + 新 packet 族"的最大简化。

### Q7. capacity / packet layout

`PickResultPacket`（`#pragma pack(1)`，与既有 ABI 同风格）：

| 字段 | 类型 | 字节 |
|---|---|---|
| `struct_size` | `uint32` | 4 |
| `status` | `int32` | 4 |
| `frame_id` | `uint64` | 8 |
| `request_id` | `uint64` | 8 |
| `entity_id` | `uint32` | 4 |
| `reserved` | `uint32` | 4 |
| **合计** | | **32** |

- **定长、极小**：与 P2 的 1,920,016B 像素包形成鲜明对比 —— pick 只需一个像素的
  readback，**不需要**把 ID 表面搬过 ABI。这是刻意的容量纪律。
- `struct_size` 首字段沿用既有 ABI 的自描述惯例（`RuntimeResult` 已有）。
- 预留 4B `reserved` 给未来字段，避免再升版本。

### Q8. ABI v1.2 还能容纳，还是必须 v1.3？

**必须 v1.3。** 依据既有纪律而非偏好：

- P2 在**只增加一个 op + 一个 fetch + 新 status** 时就把 minor 从 1 升到 2。
- 本次增加：op 29（`OP_COUNT` 29→30）+ 一类新的 GL pending edit +
  一组新的 result status + 一个新 packet。
- 因此 `ENGINE_HOST_ABI_VERSION` → `((1 << 16) | 3)`。

> **明确不做**：不修改 `RuntimeResult`（仍 **76B**）。pick 走既有 op seam +
> 独立 packet，避免污染 P2 已冻结的结构。

---

## 5. 冻结后的整体形状

```text
Pick target:     R32I / existing m_PickTexture (GL_COLOR_ATTACHMENT1)
Sentinel:        -1
Valid ID:        >= 1   (0 reserved)
Reserved:        0
Driver:          Application::PumpOneFrame 的 production render（同帧）
Thread:          runtime thread（UI thread 无 GL）
Frame identity:  result 必带 frame_id（D3-C4）
Shader:          dedicated pick pass
Y semantics:     py = H - 1 - y（已由 known-pixel 实测冻结）
OOB:             reject, never clamp
Request:         Dispatch(GL op) → pending id（异步受理）
Result:          Fetch → PickResultPacket(32B) { frame_id, request_id, status, entity_id }
Pending capacity: 1，重复请求返回明确 busy，不静默覆盖
ABI:             v1.2 → **v1.3**（additive；RuntimeResult 不变，仍 76B）
```

---

## 6. 本 contract 未回答 / 留给实现前的问题

1. `PICK_BUSY` 的确切 status 数值与 `RuntimeResult.stage` 归属。
2. pending 结果在 host `Stop` / teardown 时如何作废（必须**不得**返回陈旧 result）。
3. managed 侧如何在 UI 线程与 pump 之间配对（属 Step 4 实现，不属本 contract）。
4. **preflight（证据纪律，重要）**：任何 D3 gate 在采信结果前，必须先**证明被测
   managed/native artifact 是当前构建**（比较 artifact 与最新 source 的时间戳 /
   显式重链），**不得**只凭某个 build 命令的历史 exit code。
   本轮已因 stale `EditorBridge.dll` 误判过一次（详见 Evidence-Baseline）。

---

## 7. 边界

**零代码**：本文件未修改任何 native / managed / ABI / I2。
当前仍为 ABI **v1.2**、**I2 251**。transport 实现**未批准**。
