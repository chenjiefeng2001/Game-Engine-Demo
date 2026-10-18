# HRC-3 P2 — Atomic Frame Fetch / ABI v1.2（native contract 层）

> **阶段**：UI-HRC-3 / **P2-Native contract**（第一层）
> **日期**：2026-09-28
> **状态**：`P2-NATIVE IMPLEMENTED` — native 契约已实现并有验收证据
> **未开始**：`P2-Managed fetch` 镜像、`P2 UI proof`（viewport 呈现）
> **前置**：`docs/Editor-Avalonia-HRC3-P1-Investigation-Report.md`（P1 CLOSED）
> **基线**：`docs/Evidence-Baseline.md`（42 → 44 / I2 241 → 243）

---

## 1. 这一层做了什么 / 没做什么

**做了**（严格限于 native）：

```
production frame → presentation FBO → runtime-thread readback
                → new EngineHost_Fetch op → atomic packet
```

**没做**（分层保持，不混合）：

- ❌ managed 侧 fetch 镜像（`EditorHostService` / `EditorBridgeApi` 的 op 常量与方法）
- ❌ `WriteableBitmap` / viewport 呈现 / UI gate
- ❌ D3 Pick、D4 Resize、A8
- ❌ 任何新 export function
- ❌ 任何对 P1 已证明的 presentation FBO / scene frame 的重新实现

> ⚠ **唯一越出纯 native 的改动**：managed 的 `EngineHostExpectedVersion` 由 v1.1 同步为 v1.2。
> 原因是 `EditorHostService` 在版本不匹配时**直接抛异常**，不同步会让编辑器**无法 bootstrap**。
> 这属于"ABI/version 更新"，是本次 slice 明确包含的收尾项，不是 P2-Managed 功能的开始。
> 除此之外 managed 侧**零功能改动**。

---

## 2. ABI increment：v1.1 → v1.2（明确记录）

| 项 | 变化 |
|---|---|
| `ENGINE_HOST_ABI_VERSION` | `((1<<16)\|1)` → **`((1<<16)\|2)`** |
| 新增 op | `ENGINE_HOST_OP_GET_PRESENTATION_FRAME = 28`（**Fetch-only**） |
| `ENGINE_HOST_OP_COUNT` | 28 → **29** |
| 新增 status | `ENGINE_HOST_STATUS_NO_NEW_FRAME = 11`（**追加**取值） |
| 既有 op 0..27 | **编号、容量、语义全部不动** |
| 既有 status 0..10 | **全部不动** |
| `RuntimeResult` | **未改**：仍 76B，字段/顺序/packing 全不变 |
| export function | **未新增**（复用既有 `EngineHost_Fetch`） |
| `EditorBridge_GetVersion` | **未变**（EditorBridge 自身仍 v1.1，与 EngineHost 是两套版本号） |

**因 increment 必须同步的既有断言**（不是绕过，是让断言跟随单一来源）：

| 位置 | 原值 | 现值 |
|---|---|---|
| `SkeletonLifecycle` | 硬编码 `(1<<16)\|1` | `ENGINE_HOST_ABI_VERSION` |
| `CreationThreadContract` | 硬编码 `(1<<16)\|1` | `ENGINE_HOST_ABI_VERSION` |
| `result.abi_version` 断言 | 硬编码 `(1u<<16)\|1u` | `static_cast<uint32_t>(ENGINE_HOST_ABI_VERSION)` |
| `EditorBridgeApi.EngineHostExpectedVersion` | `(1<<16)\|1` | `(1<<16)\|2` |

---

## 3. Packet 契约（静态容量，无协商、无分配）

尺寸在编译期固定（`ENGINE_HOST_PRESENTATION_WIDTH/HEIGHT`，D4 已后置）：

```c
offset  0  int32   width      = 800
offset  4  int32   height     = 600
offset  8  uint64  frame_id   ← 与像素在同一次 runtime-thread Submit 内取得
offset 16  uint8   rgba[800*600*4]   ← GL 原生布局：行优先、左下角起
```

容量常量：`ENGINE_HOST_FRAME_PACKET_HEADER_BYTES = 16`、
`ENGINE_HOST_FRAME_PACKET_BYTES = 16 + 800*600*4 = 1,920,016`。

**原子性论证**：fetch 由 `Fetch()` 的 `Submit` 驱动 ⇒ 必定在 **runtime 线程**执行；
帧号只在**同一 runtime 线程**的 frame 命令里前进，命令队列串行化 ⇒
`frame_id` 与像素**不可能来自不同帧**。

**不变量遵守情况**：

| 不变量 | 实现方式 |
|---|---|
| 只新增一个 additive op | `ENGINE_HOST_OP_GET_PRESENTATION_FRAME`，走既有 `EngineHost_Fetch` |
| 不新增 export function | 未新增任何 `EDITOR_BRIDGE_API` 符号 |
| `RuntimeResult` 仍 76B | 结构体未触碰（用例断言 `sizeof == 76`） |
| 固定 800×600 → 静态容量 | 编译期常量；实现里**校验实际尺寸等于常量**，不等即拒 |
| `frame_id + pixels` 原子 | 同一次 Submit |
| no-new-frame 明确状态 | 返回 0 + `NO_NEW_FRAME`，**一个字节都不写** |
| GL 读回永远在 runtime 线程 | Submit 内 make-current → 读回 → uncurrent |
| `RequiresGraphicsContext` 正确标记 | 该 op **确实**需要 GL context，已标记 `true` |
| 不重新实现 P1 的 FBO / frame | 只**读**已有的 `m_PresentationFbo` |

### 3.1 `RequiresGraphicsContext` 与 Fetch-only 的处理

`RequiresGraphicsContext(op)` 用于把 **Dispatch** op 路由进 pending-edit 队列。
本 op 确实需要 GL context，因此该表**必须**标记它 —— 但那样会让 `Dispatch` 把它排队成 edit。

解决：新增 `IsFetchOnlyOp(op)`，`Dispatch` 在查表**之前**显式拒绝：

```cpp
if (IsFetchOnlyOp(op)) {  // "operation is fetch-only; use EngineHost_Fetch"
    ... reject ...
}
if (RequiresGraphicsContext(op)) { return EnqueuePendingEdit(...); }
```

⇒ `RequiresGraphicsContext(GET_PRESENTATION_FRAME) == true`（标记正确、可被测试断言），
同时该 op **无法**经 `EngineHost_Dispatch` 误用。

---

## 4. 语义矩阵

| 场景 | 返回值 | `result.status` | 是否写 packet |
|---|---|---|---|
| 未开项目（因此还没有任何帧） | `0` | `NO_NEW_FRAME` | ❌ 零字节 |
| 已开项目但尚未 pump 任何帧 | `0` | `NO_NEW_FRAME` | ❌ 零字节 |
| 有新帧 | `1,920,016` | `SUCCESS` | ✅ header + 全部像素 |
| 刚 fetch 过、未再 pump | `0` | `NO_NEW_FRAME` | ❌ **不重发旧像素** |
| 又 pump 一帧 | `1,920,016` | `SUCCESS` | ✅ `frame_id` 前进 |
| 容量不足 | `-1` | `INVALID_STATE` | ❌ |
| 经 `EngineHost_Dispatch` 误用 | `-1` | `INVALID_STATE` | ❌ |

**"没有新帧"不是失败**：它是一个明确的成功语义状态。实现上专门在 `Fetch()` 里
阻止了 `SetResult(..., SUCCESS, ...)` 对 `NO_NEW_FRAME` 的覆盖。

---

## 5. 验收证据

新增两个 native contract 用例（`test_bridge`）：

1. `P2AbiVersionIncrementIsAdditiveOnly` —— ABI 增量"只增不改"的静态断言
   （v1.2、op 28、count 29、既有 op 27/26 不变、status 10/11、`sizeof(RuntimeResult)==76`、
   header 16B、packet 容量公式）。
2. `P2FrameFetchIsAtomicAndSignalsNoNewFrame` —— 端到端：
   Dispatch 拒绝 → 无帧时 no-new-frame 且零字节 → 容量不足被拒 →
   pump 后取到 `frame_id` **等于** `PumpOneFrame` 的 `frame_id`（原子性）→
   像素含真实 scene 内容 → 不 pump 则 no-new-frame 且不重发 → 再 pump 则 `frame_id` 前进。

**执行结果**：

| 项 | 结果 |
|---|---|
| `test_bridge` Debug × 10 | **10/10 绿**，每次 44/44 |
| `test_bridge` Release × 10 | **10/10 绿**，每次 44/44 |
| `test_hrc` | 15/15，exit 0 |
| `test_scripting` | 27/27，exit 0 |
| `dotnet build` | exit 0（仅既有警告） |
| `git diff --check` | 0 |
| 残留进程 | 0 |

**过程中修正的一处用例期望错误**：我最初断言"未开项目时 fetch 返回 -1"。
实测返回 `0 / NO_NEW_FRAME` —— 这是**正确**语义（此刻确实不存在可交出的新帧），
已把断言改为 no-new-frame + 零字节。实现未因此改动。

---

## 6. 基线变更

| | 变更前（保留） | 变更后（当前） |
|---|---|---|
| `test_bridge` active | 42 | **44**（+2） |
| DISABLED | 0 | 0 |
| I2 合计 | 241 | **243** |

---

## 7. 下一步（需单独批准）

1. **P2-Managed fetch 镜像**：`EditorBridgeApi` 增加 op/status 常量与 `EngineHost_Fetch` 绑定，
   `EditorHostService` 提供解码 → `WriteableBitmap` 的路径。**不含** UI 呈现。
2. **P2 UI proof**：viewport 控件真正显示当前 EngineHost runtime frame（D1 目标）。
3. **D3 Pick** / **D4 Resize** / **A8**：继续独立冻结。

**分层不变**：P2-Native contract ✅ → P2-Managed fetch → P2 UI proof。
