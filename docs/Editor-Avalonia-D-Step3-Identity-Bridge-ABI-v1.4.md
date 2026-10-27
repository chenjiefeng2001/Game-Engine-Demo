# Editor Avalonia — Slice D Step 3：Identity Bridge（ABI v1.4 / P-1）

> **阶段**：Slice D / **Step 3（P-1 实施 + 永久回归）**
> **日期**：2026-10-21
> **状态**：`D STEP-3 CLOSED` —— `entity_id` → 场景索引的桥已落地，`--prodpick` 全绿
> **前置**：D Step 1（审计）、D Step 2（通路 + fail-closed seam）
> **授权**：P-1 已批准 —— `reserved` 正式定义为 `entity_index`，ABI `v1.3 → v1.4`

---

## 1. 变更内容（一句话）

`PickResultPacket` 的 **32 字节布局不变**，但 offset 28 从"必须为 0 的 `reserved`"
正式变为 **`entity_index`**：native 在**产生结果的那一帧**用 `Scene::FindByID` 把
`GameObject::ID` 解析成场景索引，managed 直接使用该字段（**不再**用 `id - 1` 推导）。

```text
pick pass 写入 GameObject::ID
    ↓  ServicePickRequestForFrame（同一帧、context current）
ResolveSceneIndexForEntityId(id)
    ├─ Scene::FindByID(id) == nullptr  ⇒ ENGINE_HOST_PICK_INDEX_NONE（不猜）
    └─ 找到 ⇒ 在 GetObjects() 中定位下标 ⇒ packet.entity_index
    ↓
managed: PickResult.EntityIndex → PacketPickIdentityResolver → 选中
```

---

## 2. 实施清单

| 层 | 文件 | 改动 |
|---|---|---|
| ABI | `bridge/include/editor_bridge/capi.h` | `ENGINE_HOST_ABI_VERSION` `(1<<16)\|3` → `(1<<16)\|4`；`reserved` → `entity_index`；新增 `ENGINE_HOST_PICK_INDEX_NONE = 0xFFFFFFFFu`；注释说明"为什么不能由调用方推导" |
| Native | `bridge/src/EngineHost.h` / `.cpp` | 新增 `ResolveSceneIndexForEntityId(uint32)`（public test-seam）；`ServicePickRequestForFrame` 在 HIT 时写入索引，非 HIT 一律写 NONE；新增 6 条 `static_assert`（32B + 全部 offset） |
| Native 测试 | `tests/test_bridge/test_pick_transport.cpp` | 新增 `D3PickPacketLayoutIsStableInV14`、`D3PickEntityIndexTracksIdentityAcrossDeletion`（**永久回归**）、`D3PickIdentityResolutionFailsClosed`；`test_editor_capi` 的版本断言 → v1.4 |
| Managed | `editor_avalonia/Native/EditorBridgeApi.cs` | `EngineHostExpectedVersion` → v1.4；新增 `EngineHostPickIndexNone` |
| Managed | `editor_avalonia/Services/PickResult.cs` | `OffsetReserved` → `OffsetEntityIndex`；新增 `EntityIndex` / `IndexNone` / `HasResolvedIndex`；**移除** `reserved == 0` 断言，改为"非 HIT 带真实索引 ⇒ 拒绝" |
| Managed | `editor_avalonia/Services/PickIdentity.cs` | `UnavailablePickIdentityResolver` → **`PacketPickIdentityResolver`**（读字段 + 三条 fail-closed 校验） |
| Managed | `editor_avalonia/ViewModels/MainViewModel.cs` | seam 签名改为 `TryResolve(PickResult, ...)`；新增 `LastPickEntityIndex`；`ApplyPickHit` 使用 packet 索引 |
| Managed gate | `editor_avalonia/Services/ProductionPickGate.cs` | P5 改为**生产** resolver（无注入）；P8b 改为**永久回归**；新增 P9（v1.4 解码/解析契约）；P10 → v1.4 |
| Managed gate | `D3ManagedPickGate.cs` / `P2ManagedFrameGate.cs` | 版本断言 → v1.4；M2 的合成 packet 按新契约更新，并新增"索引原样使用 / NONE 不可解析"断言 |

**未新增**：ABI 导出、operation ID、status ID、native 测试 target。
**未修改**：packet 布局（仍 32B）、`RuntimeResult`（仍 76B）、任何既有 op 语义、
生产 runtime 行为（除 `entity_index` 的填充）。

---

## 3. 四条边界的落实方式

### 3.1 v1.4 不会被静默当作 v1.3

两边都是**等值**校验，因此任一方向的混搭都被拒绝：

| 校验点 | 行为 |
|---|---|
| `EngineHostChannel.Create()` | `version != EngineHostExpectedVersion` ⇒ 抛 `HostOperationException`（带 got/expected） |
| `D3ManagedPickGate.M1` | managed 声明必须 `== v1.4`，且 native 实报必须 `== managed` |
| `D3ManagedPickGate.M8B` | **实际加载的 DLL** 报告的版本必须 `== v1.4`（捕获"部署了旧 DLL"） |
| `test_bridge` | `EngineHost_GetApiVersion() == ENGINE_HOST_ABI_VERSION` |

> 因此 **v1.3 native + v1.4 managed** 与 **v1.4 native + v1.3 managed** 都在
> `Create()` 处失败，不存在"新版本被当成旧版本"的路径。

### 3.2 `Scene::FindByID` 的语义覆盖

`ResolveSceneIndexForEntityId` 的四条早退（全部返回 `INDEX_NONE`，绝不退化成 0）：

| 情形 | 结果 | 覆盖 |
|---|---|---|
| `entityId == 0` | NONE | `D3PickIdentityResolutionFailsClosed` |
| 无 session / 无场景快照 | NONE | 同上（打开工程之前） |
| `FindByID` 未命中 | NONE | 同上（未知 id） |
| `FindByID` 命中但遍历未命中（并发修改） | NONE | 代码路径（防御性） |

NONE **不是 0** 的原因写在两处（`capi.h` 与 `PickResult.cs`）：0 是**合法的**第一个场景索引，
用它当哨兵会让"第一个实体"与"无法解析"混同。

### 3.3 索引的生命周期

- `entity_index` 表示**产生结果的那一帧**的场景快照位置；native 不缓存、不承诺跨帧一致。
- 跨帧稳定性由既有 `frame_id` 承担：`TryConsumeViewportPick(presentedFrameId)` 在
  `result.FrameId < presentedFrameId` 时判 `stale-frame` 并**不应用**。
- 索引到了 managed 之后还要按**当前投影**再校验一次：`Entities` 中找不到该 index
  ⇒ `stale-entity`，不改变选中。这两道闸合起来覆盖"结果产生 ↔ UI 消费之间场景变化"。

### 3.4 布局未变

- `static_assert`：`sizeof(PickResultPacket) == 32` 且 offset `0/4/8/16/24/28` 全部固定
  （编译期失败，而不是运行期读到错位数据）。
- `test_pick_transport` 的 `D3PickPacketLayoutIsStableInV14` 再做一遍运行期断言
  （含 `ENGINE_HOST_PICK_INDEX_NONE == 0xFFFFFFFF` 且 `!= 0`）。
- managed 解码器按同一组冻结 offset 读取；`D3-M2` 断言 `Marshal.OffsetOf == 0/4/8/16/24/28`。

---

## 4. 永久回归：P8b（这是 P-1 存在的理由）

P8b 不再是一次性诊断，而是**两层永久断言**：

### 4.1 Native（`test_pick_transport`）

`D3PickEntityIndexTracksIdentityAcrossDeletion`：

```text
pick 像素 → HIT(id=I, index=K)，K > 0
删除索引 0
同一像素再 pick → HIT(id=I, index=K-1)
且 name(K-1) == name(K) 的删除前值（按名字证明是同一个实体）
且 index **移动了**（任何"只用 ID 推导"的实现都不会移动）
```

### 4.2 Managed（`--prodpick` P8b）

同样的场景走真实 UI 路径，实测输出：

```text
[PROD-PICK GATE] P5:  hit id=9 -> index 8 'Wall_W' (production resolver, no injection)
[PROD-PICK GATE] P6:  no-hit is a normal result and clears the selection
[PROD-PICK GATE] P8b: identity regression OK - same pixel, same entity 'Wall_W',
                      index 8 -> 7 (id 9 unchanged)
[PROD-PICK GATE] P9:  v1.4 contract verified (index used verbatim, NONE refused,
                      non-HIT+index rejected)
[PROD-PICK GATE] P7:  out-of-frame clicks are rejected (never clamped)
[PROD-PICK GATE] ALL GREEN
```

> 与 Step 2 的旧 P8b 对比：旧版证明的是"`id-1` 会选错"（`Wall_W` → `Wall_E`）；
> 新版证明的是"新字段**不会**选错"（仍是 `Wall_W`，索引从 8 变成 7）。
> 同一个场景、同一个像素，断言方向从"危害"翻转为"正确"。

### 4.3 为什么它不会随场景数据失效

断言只依赖三条与内容无关的性质：
1. 删除前面的实体会让后续索引**整体前移**（容器语义）；
2. `GameObject::ID` 是 assign-once（`s_NextID` 进程级单调）；
3. 被点中的实体在删除后**仍然存在**（用例只选 `index > 0` 的命中）。

因此即使 `Main.scene` 换成别的 10 个实体、或实体名字变了，用例仍然成立
（名字只用于"是不是同一个实体"的交叉验证，不写死具体值）。

---

## 5. 验收对照

| 门禁项 | 结果 |
|---|---|
| 命中 `Wall_W` 解析到正确场景实体 | ✅ `P5: hit id=9 -> index 8 'Wall_W'` |
| 删除索引 0 后仍解析到实际命中实体（而非旧索引邻居） | ✅ `P8b: index 8 -> 7, id 9 unchanged` |
| `FindByID` 失败时 UI 不改变选择 | ✅ native `INDEX_NONE`（3 条失败路径）+ managed `P9`（NONE 不可解析 ⇒ `hit-unresolved` ⇒ 不改选中） |
| 请求/取回之间场景变化 | ✅ `stale-frame`（frame_id 比较）+ `stale-entity`（当前投影校验） |
| ABI v1.4 双端一致、packet 仍 32B | ✅ M1 / M8B / `D3PickPacketLayoutIsStableInV14` / `D3-M2` |
| Debug+Release：`test_pick` / `test_bridge` / `--prodpick` | ✅ 见 §6 |

---

## 6. 回归（Debug + Release）

| 类别 | Debug | Release |
|---|---|---|
| `--prodpick` | ALL GREEN, exit=0 | ALL GREEN, exit=0 |
| `--d3managed` | M1–M7 + M8-B ALL GREEN, exit=0 | ALL GREEN, exit=0 |
| `--pumpowner` / `--pendingedit` / `--selstatus` | ALL GREEN, exit=0 | ALL GREEN, exit=0 |
| `--p2frame` / `--p2ui` / `--prodpath` / `--hrc2` / `--uistate` | ALL GREEN | — |
| `test_bridge` | **44/44** | **44/44** |
| `test_pick` | **8/8** | **8/8** |
| `test_pick_transport` | **9/9**（原 6 + 3 新） | **9/9** |
| `test_hrc` / `test_scripting` | 15/15 · 27/27 | 15/15 · 27/27 |

**I2 计数**：`test_bridge` 仍 44、`test_pick` 仍 8 ⇒ **I2 仍 251 不变**。
`test_pick_transport` 按既有规则**不计入 I2**（见 `docs/Evidence-Baseline.md` 的记录），
本次在它内部 6 → 9，因此"含 transport"的总数 257 → 260，但**受门禁校验的 I2 仍是 251**。

**fixture**：`assets/gp01/Main.scene` md5 仍 `faabc37448f44197d6966708532c1ef9`（10 entities）；
`test_pick_transport` 的删除只作用于内存中的会话，不落盘。

### 6.1 integrity gate 实跑（`-SkipBuild`，Debug）

P-1 涉及的四个目标在 I2 门禁下**全部 ok**：

```text
  ok    test_bridge     44 tests
  ok    test_hrc        15 tests
  ok    test_pick        8 tests
  ok    test_scripting  27 tests
```

门禁同时报出 **7 项既有失败，与 P-1 无关**（本 slice 未触碰这些 target）：

| 既有失败 | 性质 |
|---|---|
| `test_content` baseline=42 / actual=60 | 工作树里新增了测试但未更新基线（本 slice 之前的漂移） |
| `test_core` baseline=20 / actual=43 | 同上 |
| `test_renderer` baseline=9 / actual=14 | 同上 |
| `DF03` / `DF05` / `DF06` / `DF07` 失败 | 依赖被 `.gitignore` 排除且缺失的 dogfood fixture（与 `Main.scene` 同一类问题） |

> 这些不是本 slice 造成的，也不应被"P-1 全绿"掩盖。它们属于既有的
> **baseline drift + 缺失 fixture** 问题面，应与 R1/R2 一起处理。

---

## 7. 遗留与下一步

- **E（动态 viewport 尺寸 / FBO 生命周期）**：按既定顺序，Step 1 先做审计与契约冻结。
  届时 `PresentationFrame.ExpectedWidth/Height` 会变成运行时值，`ViewportPickMapper`
  需要显式尺寸参数 —— E 的 Step 1 必须先冻结这一点。
- **命令行参数精确匹配**（独立维护 slice）：`Contains("--x")` 的子串语义，
  以及 `--uistatelegacy` 连带注册 `--uistate` 的既有隐患，仍未修。
