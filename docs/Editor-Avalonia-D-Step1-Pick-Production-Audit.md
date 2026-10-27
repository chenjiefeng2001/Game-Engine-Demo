# Editor Avalonia — Slice D Step 1：Production Pick / Input 审计与契约冻结

> **阶段**：Slice D / **Step 1（Audit + Contract Freeze）**
> **日期**：2026-10-21
> **状态**：`D STEP-1 CLOSED` — 5 项生产缺口已定性，1 项**阻塞性决策**已识别并给出最小方案
> **前置**：A/B/C CLOSED（见 `docs/Editor-Avalonia-Commit-ABC-Closure.md`）
> **本 Step 性质**：**先审计**。审计结论决定 Step 2 的实现形态，不预设"接线即可"

---

## 1. 结论摘要

1. **D 不是"接线"，是"把一条被主动封死的路径重建出来"。** production 后端上
   viewport 输入**整体被拦截**，pick 路径根本不可达。
2. **现有 pick 调用点用了错误的坐标系**（NDC），而冻结的 D3 ABI 要求
   **top-left 像素坐标**。二者不是同一个量，直接传会让每次点击落在错误的像素上。
3. **现有调用点是同步的**（`int hitEntity = ViewportPick(...)` 立刻返回），
   而 D3 的 PICK 是 **request → 下一帧 → fetch**。把"已受理"当成"已选中"
   正是 B 刚刚修掉的那类缺陷的同一形状。
4. **现有调用点把 pick 结果当成场景索引**（`Vm.Entities[hitEntity]`），
   而 D3 返回的是 `GameObject::ID`。**这不是 off-by-one，是两个不同的身份空间。**
5. **ID → 场景索引之间没有任何 ABI 通路**（详见 §4）。这是本 slice 的**唯一阻塞项**，
   不能靠"看起来对"的映射绕过。

---

## 2. 五项生产缺口（逐条带证据）

### D-A1 — production 后端上 viewport 输入被整体拦截

| 项 | 证据 |
|---|---|
| 输入入口统一走 `AcceptViewportInput()` | `MainWindow.axaml.cs:588-600` |
| 该函数在 `EngineHost` 后端**恒返回 false** | `MainWindow.axaml.cs:593`：`if (_host.SupportsViewport) return true;` … `return false;` |
| `SupportsViewport` 在 EngineHost 下恒为 `!IsEngineHost` = false | `EditorHostService.cs:221` |

**推论**：`PointerPressed` / `Moved` / `KeyDown` 等 handler 在 production 后端
**第一行就 return**。因此 D3 的 PICK 在生产路径上**从未被调用过一次**
（与 D3 Step 1 的"4 套实现 0 生产引用"结论一致，只是这里连入口都封着）。

### D-A2 — 坐标空间错误：NDC ≠ 像素

```csharp
// MainWindow.axaml.cs:602
private (float ndcX, float ndcY) PointerToNdc(PointerEventArgs e)
{
    float ndcX = (float)(pos.X / bounds.Width * 2.0 - 1.0);
    float ndcY = (float)(1.0 - pos.Y / bounds.Height * 2.0);
}
```

而 native 契约是（`OpenGLFramebuffer.cpp:236-251`）：

```text
canonical 坐标 = top-left 原点、像素单位、范围 [0,W) × [0,H)
px = mouseX - viewportMinX
py = viewportMinY - mouseY + (H - 1)
越界 → OutsideViewport（拒绝，不 clamp）
```

NDC 的 x∈[-1,1]、y 向上为正，与 canonical 的 x∈[0,W)、y 向下为正**没有任何线性关系**
能靠一个公式同时满足：NDC 的 -1 对应像素 0，但 NDC 的 **+1 对应像素 W**（越界一格），
且 NDC 的 y 方向相反。把 NDC 直接交给要求像素的 ABI 会得到**系统性错位**而不是崩溃 ——
属于静默错误。

### D-A3 — 同步假设 vs 异步契约

```csharp
// MainWindow.axaml.cs:627-633
int hitEntity = Vm.ViewportPick(ndcX, ndcY);
if (hitEntity >= 0 && hitEntity < Vm.Entities.Count)
    Vm.SelectedEntity = Vm.Entities[hitEntity];
```

而 D3 的 ABI 语义是（`capi.h:477-481`）：

- `Dispatch(PICK, [x,y])` → 只表示**已受理**（`EDIT_ACCEPTED` + `operation_id`）；
- 执行发生在**下一次** `PumpOneFrame` 内；
- 结果经 `Fetch(PICK)` 取回，带 `frame_id` / `request_id`。

因此"点击即返回命中"在 ABI 上不存在。把受理当结果 = B3 缺陷的同一形状。

### D-A4 — 身份空间错误：`GameObject::ID` ≠ 场景索引

```csharp
Vm.Entities[hitEntity]   // MainWindow.axaml.cs:631 —— hitEntity 被当成场景索引
```

`Entities` 的 `Index` 是**场景数组下标**（`GetObjects()[index]`，见
`EditorSession.cpp:502-503`）。而 D3 冻结的 `PickResultPacket.entity_id` 是
**`GameObject::ID`**（`D3-Pick-Contract-Resolution.md` §6 U5；
`SpriteComponent.cpp:117` 的 `cmd.entityId = owner->GetID()` 是它的来源）。

两者只在**从未删除过任何实体**的加载态下恰好满足 `ID == index + 1`；
删除会平移索引而不改变 ID。因此按下标取实体会**选错实体**，且不报错。

### D-A5 — 焦点从未被获取，键盘路径事实上不可达

`ViewportBorder` 声明了 `Focusable="True"`（`MainWindow.axaml:87`）与
`KeyDown/KeyUp` handler，但**全仓没有任何 `Focus()` 调用**（`grep -c "\.Focus()" = 0`）。
因此 `OnViewportKeyDown` 在生产路径上从未被 `Focusable` 语义触发过一次。

同时 `KeyDown != 全局快捷键`：即使拿到焦点，也必须**只在 viewport 持有焦点时**
消费 viewport 快捷键，否则会在文本框输入时抢键。

---

## 3. Step 2 要冻结的坐标契约（D-C1 ~ D-C5）

### D-C1 — 唯一坐标空间：presentation frame 的 top-left 像素

- canonical 空间 = **presented frame 的像素坐标**：`x ∈ [0,W)`、`y ∈ [0,H)`、top-left 原点。
- 与 D3-C3 完全一致，**不引入第二种 canonical 空间**。
- Avalonia 侧只允许做一件事：把 `Point`（相对 `ViewportImage`）转成 canonical 像素。
- **禁止**继续向 ABI 传 NDC。

### D-C2 — 映射必须由 executable assertion 冻结，不得推导

D3 Step 2 §U4 已经写明：y 公式"必须先确定 Avalonia / GL 坐标约定，并用一个
**已知像素**做 executable assertion 之后，才冻结"。

因此 Step 2 的 gate 必须**先测量**、再断言：

1. 读出 `ViewportImage.Bounds`（控件自身矩形）与 `image.Source.PixelSize`；
2. 断言"帧 1:1 绘制在控件原点"这一几何前提（`Stretch=None` + `Left/Top` 对齐）；
3. 由 `e.GetPosition(ViewportImage)` 得到的 `(x,y)` 直接作为 canonical 像素；
4. 断言 `round(x) == floor(x)` 边界行为、以及越界点击得到 `OutsideViewport` **而非** 边缘命中。

任何"帧不是 1:1 绘制在控件原点"的情形 → **`GeometryMismatch`，显式拒绝 pick**，
而不是悄悄按某个猜测的 offset 映射。

### D-C3 — 输入所有权：只有 viewport 持有焦点才消费

- `ViewportBorder` 在 `PointerPressed` 时取得焦点（`Focus()`）。
- 键盘仅当 `ViewportBorder.IsFocused`（或 `IsKeyboardFocusWithin`）为真时转发。
- 鼠标（点击/移动/滚轮）不需要键盘焦点，但**必须先通过 §D-C2 的映射**。

### D-C4 — 异步：受理 ≠ 选中

单笔在途状态机（与 B 的四态同构）：

| 状态 | 触发 | 允许改选中？ |
|---|---|---|
| `Idle` | 初始 / 结果已消费 / 被 teardown 作废 | — |
| `Pending` | `Dispatch(PICK)` 返回 `Accepted` | **否** |
| `Busy` | 已有在途请求（`PICK_BUSY`） | **否**（不替换在途请求） |
| 结果消费 | 下一帧 `Fetch(PICK)` 返回 packet | 仅 `Hit` 且身份解析成功 |

`NoHit` 是**正常结果**（该像素无实体）→ 允许清空选中；`OutsideViewport` /
`InvalidCoordinate` / `NotReady` / `Fault` → **不改选中**，只报告。

### D-C5 — 身份解析必须显式，不得猜

见 §4。Step 2 必须把"`entity_id` → 场景索引"收敛到**单一 seam**，
且在 seam 不可用时 **fail-closed**（不选中、写诊断），绝不退回 `ID == index + 1` 的猜测。

---

> **状态更新（2026-10-21）**：本节的决策**已批准**并落地为 **P-1 / ABI v1.4**
> （`reserved` → `entity_index`）。实现与证据见
> `docs/Editor-Avalonia-D-Step3-Identity-Bridge-ABI-v1.4.md`。
> 下面保留 Step 1 当时的分析与候选方案，作为决策依据的历史记录。

## 4. 阻塞性决策：`GameObject::ID` → 场景索引**没有 ABI 通路**

### 4.1 事实

| 尝试 | 结果 |
|---|---|
| 是否有 op 直接返回 `GameObject::ID` | **没有**。`ENGINE_HOST_OP_*`（`capi.h:425-483`）共 30 个取值（`NONE=0` … `PICK=29`，`COUNT=30`），无 ID 查询 op |
| `GET_COMPONENT_PROPERTY` 能否旁路 | **不能**。只有 `CameraComponent` / `ColliderComponent` 实现 `GetPropertyDesc`（`engine/src/Core/GameObject/`），都不暴露 ID |
| `EV_ENTITY_CREATED` payload 是否带 ID/索引 | **不带**。payload 是实体名（`test_bridge` 断言 `payload == "Probe"`） |
| 是否可复用 `PickResultPacket.reserved` | **需改契约**。managed 解码器**显式断言 `reserved == 0`**（`PickResult.cs:112-116`），写入索引会让它 fail-closed |
| 是否可用 `ID == index + 1` | **不可**。仅"从未删过实体"时成立；删除后静默选错 —— **Step 2 的 P8b 已把它做成可执行证据**：删除 index 0 后，同一像素的 id 仍为 9，`id-1` 仍是 index 8，而 index 8 已从 `Wall_W` 变成 `Wall_E` |

### 4.2 三个候选方案（按侵入性排序）

| 方案 | 内容 | 侵入性 | 判定 |
|---|---|---|---|
| **P-1（推荐）** | 在**既有** op 的结果 packet 里补一个字段：把 `reserved` 正式定义为 `entity_index`，managed 解码器改为读取并校验（`Hit` 时 `entity_index < entityCount`），ABI 版本 `v1.3 → v1.4` 让 M1 lockstep 抓住漂移 | **0 个新导出、0 个新 op**、0 个新 status；native 侧只在既有 `ServicePickRequestForFrame` 内多算一个索引（`Scene::FindByID` 反查下标） | 唯一同时满足"不增 ABI 表面"与"语义不猜"的方案 |
| P-2 | 把 `packet.entity_id` 改成场景索引 | 破坏 D3-C2/U5 冻结语义 + `tests/test_pick` 的"精确 `GameObject::ID`"断言 | ❌ |
| P-3 | managed 侧维护 ID→索引缓存（靠 create 事件推断顺序） | 依赖未承诺的创建顺序；删除后失效 | ❌ |

> **P-1 的代价要说清**：它不是"零 ABI 变化"，而是**零 ABI 表面增长**（没有新导出、
> 没有新 op、没有新 status、packet 大小不变）。版本号仍必须显式 +1，
> 因为 `reserved` 的既有语义是"必须为 0"，改用它是一次**真实的契约变更**，
> 必须让 lockstep gate 可检出。**是否批准 P-1 需要单独决定**，不在 Step 2 里默认执行。

### 4.3 Step 2 在决策落地前的边界

Step 2 **先把 seam 建好并 fail-closed**：

```text
IPickIdentityResolver
  ├─ Unavailable   ← 默认。返回 null + 明确诊断（"identity bridge not approved"）
  └─ IndexFromPacket / ...   ← 决策落地后再接
```

于是：点击 → 受理 → 帧 → packet → `Hit` **但身份不可解析** ⇒
**不选中、不假装成功**，Console 明确写出"hit id=N but no approved id→index bridge"。
这既满足 D-C4（受理≠选中），也满足 D-C5（不猜），并使决策落地时**只改一个 seam**。

---

## 5. 本 Step 的边界（零代码）

- 未修改任何 native / managed / shader / ABI / I2。
- 未修改 `AcceptViewportInput` 的现有行为（Step 2 才改）。
- `test_bridge` 仍 **44**、`test_hrc` **15**、`test_scripting` **27**，**I2 仍 243**，ABI 仍 **v1.3**。

---

## 6. Step 2 的交付清单（待批准后执行）

1. `ViewportPickMapper`（纯函数 + `GeometryMismatch` fail-closed）；
2. `PickIdentity` seam（默认 `Unavailable`）；
3. `MainViewModel` 的 pick 投影（`Idle/Pending/Busy` + 结果消费语义）；
4. `MainWindow` 的输入接线（focus 获取 + 像素映射 + `FrameAdvanced` 内 fetch）；
5. 生产路径 gate（`--prodpathpick`）+ runner，断言：
   几何前提、受理≠选中、`NoHit`/`OutsideViewport`/`Busy`/`GeometryMismatch` 各语义、
   焦点门控、以及**不使用** `ViewportPanel` / `EditorSession_ViewportPick`。
6. 明确记录"最终 hit 选中"一项**取决于 §4 决策**。
