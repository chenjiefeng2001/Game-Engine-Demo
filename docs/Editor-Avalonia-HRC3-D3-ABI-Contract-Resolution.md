# HRC-3 D3 ABI Contract Resolution — 四个未决项冻结

> **阶段**：UI-HRC-3 / **D3 ABI**（Contract Resolution，**docs-only，零代码**）
> **日期**：2026-09-29
> **状态**：`D3 ABI RESOLUTION CLOSED`（四项全部冻结；**v1.3 implementation 仍未批准**）
> **前置**：D3 ABI architecture FROZEN（`docs/Editor-Avalonia-HRC3-D3-ABI-Mini-Contract.md`）
> **边界**：ABI 仍 **v1.2** · **I2 仍 251**（只有 implementation 加入新 native inventory 才更新）

---

## 0. 编号复核（不复制旧表，直接读当前 `capi.h`）

| 项 | 当前实际值 | 结论 |
|---|---|---|
| `ENGINE_HOST_OP_GET_PRESENTATION_FRAME` | **28** | 已是最后一个 op |
| `ENGINE_HOST_OP_COUNT` | **29** | — |
| 下一个 additive op | — | **`PICK = 29`** |
| 新 `OP_COUNT` | — | **29 → 30** |

STATUS 实际枚举到 **11**（`NO_NEW_FRAME`）；STAGE 实际枚举到 **11**（`EDIT`）。
以下取值全部由这两条实际枚举推导，**不重新编号任何既有值**（append-only）。

---

## 1. `PICK_BUSY` 的 status / stage

### 冻结

```text
ENGINE_HOST_STATUS_PICK_BUSY        = 12    // append-only，接在 NO_NEW_FRAME(11) 之后
stage 归属 = ENGINE_HOST_STAGE_EDIT (11)   // **既有 stage，不新增**
```

### 为什么 stage 用既有的 `EDIT`

busy 的成因是**队列里已有一条未取走的 GL pick 请求** —— 这正是既有 edit/pending 队列的语义。
因此 stage 归属 `ENGINE_HOST_STAGE_EDIT` 是**语义吻合的既有值**，无需新增 stage。

### 为什么 status 必须新增（不能复用）

逐一排除既有 status：

- `EDIT_ACCEPTED(10)` 是**受理**语义，busy 是**拒绝**，相反。
- `INVALID_STATE(2)` 语义是"host 状态不允许该操作"；busy 时 host 状态完全正常，
  只是槽位已占用 —— 复用会把"状态坏了"和"队列忙了"混为一谈。
- `SKIPPED_NOT_PLAYING(1)` / `WRONG_THREAD(3)` 均不符。

⇒ 新增 append 值 **12** 是唯一诚实的选择。

---

## 2. pending result 的 teardown 语义

### 冻结的契约

> **任何 `Stop` / `Close` / terminal teardown 都使全部 pending pick request 立即失效；
> 此后 `Fetch` 不得返回旧的 `PickResult`。**

被禁止的序列（明确列为反例）：

```text
request N → Stop/Close → reopen → Fetch  →  绝不能返回 N
```

### 失效后的 wire 语义（不凭空造 status）

```text
RuntimeResult.status = ENGINE_HOST_STATUS_INVALID_STATE (2)   // 既有值
RuntimeResult.stage  = ENGINE_HOST_STAGE_SHUTDOWN (10)        // 既有值
PickResultPacket     = **不产生**（调用方 out 缓冲保持未写入）
```

| 场景 | status | stage | packet |
|---|---|---|---|
| teardown 后 Fetch | `INVALID_STATE` (2) | `SHUTDOWN` (10) | **不产生** |
| 从未 Dispatch 就 Fetch（无 outstanding） | `INVALID_STATE` (2) | `EDIT` (11) | **不产生** |

两者复用**同一个既有 status**，靠 **stage 区分**"被 teardown 作废"与"本来就没有" ——
这正是 stage 字段的用途，也避免为同一类"取不到结果"发明两个 status。

### 关键约束

**失效后不得产生任何 packet** —— 不能返回一个 `status` 为 `Fault` 的 packet 来"表达失效"。
失效是**没有结果**，不是"有一个失败的结果"。返回 packet 会诱使 managed 把它当成一次
真实的 pick 结局（D3-C6 的区分会被绕过）。

---

## 3. managed pairing 规则

### 冻结

```text
Dispatch(PICK)        → operation_id  (== request_id)
PumpOneFrame          → 在该帧内服务请求，结果绑定该帧 frame_id
Fetch(PICK_RESULT)    → packet.request_id / packet.frame_id
```

**配对规则**：`packet.request_id` **必须**等于当前 outstanding request 的
`operation_id`。不匹配 ⇒ managed **丢弃**该 packet 并按协议报错，
**禁止**把"最新结果"直接绑定成当前请求。

### 必须覆盖的三个序列

| # | 序列 | 冻结行为 |
|---|---|---|
| 1 | `Dispatch(A)` → `Fetch` | `packet.request_id == A`，正常消费 |
| 2 | `Dispatch(A)` → `Dispatch(B)` → `Fetch` | B 返回 `PICK_BUSY(12)`（**B 未被受理**）；`Fetch` 返回 **A** 的结果，`request_id == A`。managed **不得**等待 B |
| 3 | `Dispatch(A)` → `Stop/Close` → `Fetch` | 无 packet；`INVALID_STATE(2)` + `SHUTDOWN(10)`；managed **丢弃 A**，绝不呈现为一次 pick 结局 |

### 明确禁止

- 缓存"最近一次 result"并在下次请求时复用；
- 在 B 被 BUSY 拒绝后仍把 `Fetch` 的返回当作 B 的结果；
- teardown 后呈现任何来自旧 generation 的 packet。

> 序列 2 是最容易出错的一处：B 的 `PICK_BUSY` 意味着 **B 从未进入队列**，
> 因此 outstanding 仍是 A。把 A 的结果说成 B 的，就是"靠时序猜测" ——
> 正是 D3 要排除的那一类。

---

## 4. artifact freshness —— 升级为 D3 gate **contract**

### 冻结（不再是"说明"，是采信前提）

> 在任何 D3 gate 结果被采信之前，必须**证明**被测 managed bridge 与 native DLL
> 是**当前构建的配对 artifact**。

### 三种可接受的证明（满足其一）

1. **时间戳**：native DLL 的 mtime **晚于**所有相关 native source / build input；
   managed assembly 晚于所有相关 managed source。
2. **显式 build receipt**：本次 gate 运行前完成一次可验证的 relink/build receipt。
3. **artifact identity**：gate **自行打印** artifact identity/version，
   并与本次 build receipt 匹配。

### 明确不再接受

- ❌ 只看某个 build 命令的**历史 exit code**。
- ❌ 只看 managed 侧能启动（能启动不代表加载的是最新 native）。

### 落地要求

- 进入 `docs/Evidence-Baseline.md` 的 gate 条目；
- 进入 **D3 gate runner contract**：D3 runner **必须**在跑场景前执行 freshness 检查，
  **不通过则拒绝运行**（fail-closed），而不是打一条 warning 继续。

> 依据：本轮曾因 stale `EditorBridge.dll` 误判过一次 —— 应用根本没执行 D3 pick code，
> 却"复现"出一个不存在的 hang。

---

## 5. 冻结后的 v1.3 增量总表（仍待批准实现）

```text
ABI version:      v1.2 → v1.3           (additive；RuntimeResult 不变，仍 76B)
op:               PICK = 29
OP_COUNT:         29 → 30
status (append):  PICK_BUSY = 12
stage (复用):      EDIT = 11  (busy)      SHUTDOWN = 10  (teardown 失效)
packet:           PickResultPacket = 32B (已 ratified，本轮不变)
pending capacity: 1
```

## 6. 本轮边界

**零代码**：未修改 native / managed / ABI / I2。
ABI 仍 **v1.2**，**I2 仍 251**，`test_pick` 8/8，P2 standing GREEN。
**v1.3 implementation 未批准。**
