# HRC-3 P2 — Managed Fetch Mirror（第二层）

> **阶段**：UI-HRC-3 / **P2-Managed fetch mirror**
> **日期**：2026-09-28
> **状态**：`P2-MANAGED IMPLEMENTED` — M1~M7 全绿
> **未开始**：`P2 UI Proof`（`WriteableBitmap` / `ViewportImage` 呈现）
> **前置**：P1 CLOSED（`Editor-Avalonia-HRC3-P1-Investigation-Report.md`）、
> P2-Native CLOSED（`Editor-Avalonia-HRC3-P2-Fetch-Contract.md`）
> **基线**：`docs/Evidence-Baseline.md`

---

## 1. 两条 version line（冻结解释，避免被误读成 ABI 不一致）

> **`EditorBridge_GetVersion = v1.1` 与 `EngineHost_ABI = v1.2` 是两个独立 version line。**

| version line | 取值 | 归属 |
|---|---|---|
| `EditorBridge_GetVersion` / `EditorBridgeApi.ExpectedVersion` | **v1.1** | EditorBridge 自身（`EditorSession_*` 面） |
| `EngineHost_GetApiVersion` / `ENGINE_HOST_ABI_VERSION` / `EngineHostExpectedVersion` | **v1.2** | EngineHost 面（`EngineHost_*`） |

P2 的 increment **只发生在 EngineHost 这条线上**；EditorBridge 侧**没有**任何变化。
这不是不一致，而是两条线各自独立演进。gate 的 M6/M7 会显式断言 EditorBridge 线**必须仍是 v1.1**。

---

## 2. 数据流与冻结的解码契约

```
native Fetch packet
    ↓ EngineHost_Fetch（既有 export，op=28）
EditorBridge managed P/Invoke
    ↓
managed packet decoder（显式 offset）
    ↓
typed managed frame object（PresentationFrame）
```

**解码契约（冻结）**，用 `BinaryPrimitives` 逐字段**显式 offset 读取**（小端）：

| offset | 类型 | 含义 |
|---|---|---|
| 0 | `int32` | width（800） |
| 4 | `int32` | height（600） |
| 8 | `uint64` | frame_id |
| 16 | `byte[]` | RGBA8888，行优先、左下角起（GL 原生布局） |

**刻意不把 buffer reinterpret 成 managed struct** —— 避免 packing/alignment 把 native packet
布局与 managed ABI **偶合**在一起。managed 侧用 `BinaryPrimitives.ReadInt32LittleEndian` /
`ReadUInt64LittleEndian` 逐字段读，payload 用 `Array.Copy` 复制。
（`PresentationFrame.cs` 中出现 `reinterpret` / `WriteableBitmap` 等字样**仅在注释里**，
用途是声明"这些**没有**做"。）

新增文件：

| 文件 | 作用 |
|---|---|
| `Services/PresentationFrame.cs` | 类型化帧 + `TryDecode`（含全部拒绝路径）+ `FrameFetchResult` |
| `Services/P2ManagedFrameGate.cs` | M1~M7 gate |

改动：`Native/EditorBridgeApi.cs`（op/status/容量常量）、`Services/EditorHostService.cs`
（`EngineHostChannel` 上的 fetch+decode，facade 上的转发）、`MainWindow.axaml.cs`（`--p2frame` 入口）。

---

## 3. 分层纪律：managed 层**没有**做这些

| 禁止项 | 状态 |
|---|---|
| 再读 FBO | ✅ 没有（managed 完全不接触 GL/FBO） |
| 再调用 GL | ✅ 没有 |
| 自己生成 frame id | ✅ 没有（`frame_id` 一律来自 packet offset 8） |
| 缓存"最近一帧"冒充新帧 | ✅ 没有（`NoNewFrame()` 无法携带 frame） |
| 修改 pump | ✅ 没有（gate 走既有唯一入口 `RuntimeTick`，其实现未改） |
| 等待 frame completion | ✅ 没有（同步 fetch） |
| 第二条 transport | ✅ 没有（复用既有 `EngineHost_Fetch`） |
| 提前接 `WriteableBitmap` / `ViewportImage` | ✅ 没有（`MainWindow._viewportBitmap` 是既有 legacy 路径，未被本层触碰） |

---

## 4. M1 ~ M7 门禁

| Gate | 证明了什么 | 实现方式 |
|---|---|---|
| **M1** | v1.2 version lockstep | `EngineHost_GetApiVersion()` 必须等于 managed 声明的 `EngineHostExpectedVersion`，且 managed 声明必须是 v1.2；backend 必须是 EngineHost |
| **M2** | packet offsets / size 正确 | 构造合法 packet 走真实解码器：`800×600`、`header 16B`、`total 1,920,016B`、`frame_id` 取自 offset 8、payload 起于 offset 16、长度 `w*h*4`；并验证**拒绝路径**：字节数不符、header 不完整、尺寸错、`frame_id==0`、null buffer |
| **M3** | `frame_id` 与 RGBA 同包一致 | 每一帧解码出的 `frame_id` 与同 packet 的 RGBA 一起取出；像素必须是真实 scene 内容（非 clear 像素 > 0） |
| **M4** | `NO_NEW_FRAME` 不复用旧帧 | 开项目但未 pump ⇒ `HasNewFrame=false` 且 `Frame is null` 且 status 为 `NO_NEW_FRAME`；`FrameFetchResult.NoNewFrame()` 本身无法携带 frame；每帧 fetch 后立即再 fetch 必须是"无新帧" |
| **M5** | 连续新帧单调推进 | 连续 3 帧：`frame_id` 严格递增、每帧都有真实内容 |
| **M6** | native 回归全绿 | `test_bridge` 44/44（Debug+Release）、`test_hrc` 15/15、`test_scripting` 27/27 |
| **M7** | I2 baseline 无静默漂移 | gate **不**改基线、不做静默重写：把 managed 可见的清单（op=28、count=29、status=11、packet=1,920,016、abi=0x00010002）显式打印供人工比对；并断言 EditorBridge 线**仍为 v1.1** |

### 4.1 gate 输出（实测）

```text
[P2-M1] version lockstep OK (native=managed=0x00010002 = v1.2)
[P2-M2] packet offsets/size + rejection paths OK (header 16B, total 1,920,016B)
[P2-M4] NO_NEW_FRAME never reuses an old frame
[P2-M3/M5] 3 consecutive frames decoded; last frame_id=3; atomicity + monotonicity OK
[P2-M6/M7] managed-visible inventory: opGetPresentationFrame=28 (opCount=29),
          statusNoNewFrame=11, packetBytes=1920016, abi=0x00010002
[P2-M6/M7] inventory explicit (no silent baseline rewrite); EditorBridge line still v1.1
[P2-MANAGED GATE] ALL GREEN
p2frame exit=0
```

---

## 5. 执行证据

| 项 | 结果 |
|---|---|
| `--p2frame` managed gate（Debug）× 8 | **8/8 ALL GREEN**，exit 0 |
| `test_bridge` Debug / Release | **44/44**，exit 0（ABI v1.2 + 生产渲染门控 ON） |
| `test_hrc` | 15/15，exit 0 |
| `test_scripting` | 27/27，exit 0 |
| `dotnet build` | exit 0 |
| `git diff --check` | 0 |
| 残留进程 | 0 |

**过程中修正的两处我自己的期望错误**（实现未因此改动）：

1. M2 里我先断言"合成 packet 只有 1 个非 clear 像素"，但合成 buffer 的 payload 是**全 0**，
   而 clear 是 `(13,13,18)` ⇒ 480,000 个像素**全都**算非 clear。已改为先把 payload 填成 clear
   color、再改一个像素，期望值才精确。
2. `RuntimeResult` 在 managed 侧字段名是 `Status`（PascalCase），我先按 native 的 `status` 写，
   编译报错后改正。

---

## 6. 基线

| | 值 |
|---|---|
| `test_bridge` active | **44**（**未变** —— 本层没有新增 native 测试） |
| 新增 managed gate | `--p2frame`（M1~M7），**不计入 I2 native 清单**，显式记录 |
| I2 合计 | **243**（**未变**） |

> 遵守 M7：managed 门禁**不**静默改写基线。native 清单未变，因此 I2 不变；
> 新的 managed gate 单独记录在此，不混入 native 计数。

---

## 7. 下一步（需单独批准）

> **P2 UI Proof — `WriteableBitmap` / `ViewportImage`**

这一层才回答"UI **真的显示**这个 frame"（D1 目标）。当前状态是：frame 已经能
**原子取到、正确解码、逐帧单调推进**，但**尚未被呈现到任何 UI 控件上**。

D3 Pick / D4 Resize / A8 继续独立冻结。
