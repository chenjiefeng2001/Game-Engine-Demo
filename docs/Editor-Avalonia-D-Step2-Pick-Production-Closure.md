# Editor Avalonia — Slice D Step 2：Production Pick / Input 实现与收口

> **阶段**：Slice D / **Step 2（实现 + 生产路径 gate）**
> **日期**：2026-10-21
> **状态**：`D STEP-2 CLOSED` —— 除"最终 hit→选中"外全部落地并双配置实测
> **前置**：D Step 1 CLOSED（`docs/Editor-Avalonia-D-Step1-Pick-Production-Audit.md`）
> **阻塞项**：`GameObject::ID` → 场景索引的 identity bridge **仍未批准**（见 §5）

---

## 1. 本 slice 做了什么（一句话）

把 production（EngineHost）后端上**被主动封死**的 viewport 输入与 pick 路径重建为
一条**可执行、可证明、fail-closed** 的链路：

```text
Avalonia 点击（相对 ViewportImage）
    ↓  ViewportPickMapper（canonical 像素；越界拒绝，不 clamp）
Dispatch(PICK)                         ← 既有 op 29，未新增
    ↓  （下一次 pump 帧内执行 —— 既有唯一 pump）
Fetch(PICK) → PickResultPacket         ← 既有 transport
    ↓  PickIdentity seam（当前 Unavailable ⇒ fail-closed）
selected ?  ← 仅在 seam 可用且 index 有效时
```

---

## 2. 文件清单与"非修改范围"

### 2.1 新增

| 文件 | 作用 |
|---|---|
| `editor_avalonia/Services/ViewportPickMapper.cs` | canonical 像素映射（纯函数）+ `PickMapStatus` |
| `editor_avalonia/Services/PickIdentity.cs` | `IPickIdentityResolver` seam + `UnavailablePickIdentityResolver` |
| `editor_avalonia/Services/ProductionPickGate.cs` | 生产路径 gate（P1~P10） |
| `tools/run_production_pick_gate.ps1` | standing runner（`--prodpick`） |
| `docs/Editor-Avalonia-D-Step1-Pick-Production-Audit.md` | Step 1 审计 |

### 2.2 修改

| 文件 | 改动 |
|---|---|
| `editor_avalonia/MainWindow.axaml` | `ViewportImage` 显式 `HorizontalAlignment=Left/VerticalAlignment=Top`（D-C2 几何前提）；新增 `--prodpick` 入口 |
| `editor_avalonia/MainWindow.axaml.cs` | 输入所有权拆分（pointer / keyboard / legacy capability）；删除 `PointerToNdc`；新增 `RequestPickAtPoint`；`OnFrameAdvanced` 内消费 pick |
| `editor_avalonia/ViewModels/MainViewModel.cs` | pick 投影（`Idle/Pending/Busy` + 结果语义 + 诊断）；`ClearProjectProjection` 同时作废在途 pick |
| `docs/Evidence-Baseline.md` | 追加本 slice 证据 |

### 2.3 明确**未**修改（边界）

| 项 | 状态 |
|---|---|
| native（`bridge/**`、`engine/**`） | **零改动** |
| ABI 版本 / 导出 / op / status / packet | **零改动**（仍 v1.3、导出数不变、`PICK=29`、`OP_COUNT=30`、packet 32B） |
| 新 native 测试 | **0**（I2 仍 243） |
| legacy `ViewportPanel` / `EditorSession_ViewportPick` | **未触碰、未用作 fallback** |
| `assets/gp01/*` fixture | **未写入**（gate 不 Save、不写 scene） |
| 新 pump / 新定时器 | **0**（pick 结果走既有 `FrameAdvanced`） |

---

## 3. 坐标契约（D-C1 / D-C2）——实测后冻结

### 3.1 冻结内容

```text
canonical 空间 = presented frame 的像素坐标
                 top-left 原点、[0,W) × [0,H)、半开区间
pointer → canonical：x = floor(pointerX), y = floor(pointerY)
                     其中 pointer 相对 ViewportImage
越界：OutsideFrame（拒绝；**不 clamp**）
非有限：InvalidCoordinate
前提：Stretch=None + 控件原点锚定（1:1，无缩放）
```

### 3.2 实测推翻了"控件尺寸 == 帧尺寸"这一直觉前提

第一次实现把前提写成 `image.Bounds == 800×600`，实测直接红：

```text
[PROD-PICK GATE] FAIL: P1: ViewportImage must be sized to the frame
   (Stretch=None + Left/Top); got 554x600 vs 800x600
```

**原因是窗口比帧窄**：`Left/Top` + `Stretch=None` 下控件被布局裁剪到 554 宽，
但**可见区域仍然逐像素对齐**。因此：

- "控件小于帧" = 裁剪 = **合法**；
- 真正要拒绝的是**缩放**（帧被拉伸绘制），用 `Stretch` 判定，**不从尺寸反推**
  （从尺寸反推会把"裁剪"误判成"缩放"，也会漏掉"缩小到控件内"的 Uniform 情形）。

这正是 D3 Step 2 §U4 要求"用 executable assertion 冻结常数"的用途：
公式看似显然，前提却写错过一次。

### 3.3 floor 而不是 round

像素 `p` 覆盖 `[p, p+1)`。用 `round` 会让 `p+0.5` 归到 `p+1`，使像素边界上的点击
落到相邻像素 —— 与 canonical 的半开区间语义不符。gate 有专门断言
（`100.9 → 100`）。

---

## 4. 异步语义（D-C4）——受理 ≠ 选中

与 Commit B 的四态同构：

| 状态 | 触发 | 改选中？ |
|---|---|---|
| `Idle` | 初始 / 结果已消费 / 被 teardown 作废 | — |
| `Pending` | `Dispatch(PICK)` 返回 `Accepted` | **否** |
| `Busy` | native 报 `PICK_BUSY` | **否**（不替换在途请求） |

结果分类（全部**显式**，不合并）：

| native status | VM 结论 | 改选中 |
|---|---|---|
| `HIT` + seam 可用 + index 有效 | `hit` | 是 |
| `HIT` + seam 不可用 | `hit-unresolved` | **否**（fail-closed） |
| `HIT` + index 已不存在 | `stale-entity` | 否 |
| `NO_HIT` | `no-hit` | 清空选中（正常结果，不是失败） |
| `OUTSIDE_VIEWPORT` | `outside-viewport` | 否 |
| `INVALID_COORDINATE` | `invalid-coordinate` | 否 |
| `NOT_READY` / `FAULT` | `not-ready` / `fault` | 否 |

额外的 stale-frame 判别：`result.frame_id < presentedFrameId` ⇒ `stale-frame`，不应用。

---

## 5. 阻塞项：identity bridge —— **已在 Step 3 关闭**

> **状态更新（2026-10-21）**：P-1 已批准并落地为 **ABI v1.4**，
> 详见 `docs/Editor-Avalonia-D-Step3-Identity-Bridge-ABI-v1.4.md`。
> `PickIdentity` 的默认实现已从 `UnavailablePickIdentityResolver` 换成
> `PacketPickIdentityResolver`（读取 native 解析好的 `entity_index`），
> P5 因此改为**生产 resolver、无注入**，P8b 从"危害证明"翻转为"正确性回归"。
> 以下内容保留为 Step 2 当时的决策记录，不再代表当前实现。

### 5.0 Step 2 当时的状态（历史记录）

### 5.1 现状

`PickResultPacket.entity_id` 是 **`GameObject::ID`**，UI 需要**场景索引**。
二者之间**没有任何 ABI 通路**（Step 1 §4.1）：
没有返回 ID 的 op、`GET_COMPONENT_PROPERTY` 无组件暴露 ID、`EV_ENTITY_CREATED` 只带名字、
`reserved` 被 managed 解码器显式断言为 0。

因此 `PickIdentity` 的默认实现 `UnavailablePickIdentityResolver`：

```text
PICK HIT UNRESOLVED: entity id 9: hit id is a GameObject::ID, but no approved
id->scene-index bridge exists ... (the selection is unchanged)
```

**不猜、不退回 `id - 1`。**

### 5.2 本 slice 用**实证**证明"猜"为什么不行（P8b）

gate 不只是"没实现"，而是把危害做成了可执行证据：

```text
[PROD-PICK GATE] P5:  hit id=9 at (40,296) -> selected 'Wall_W' (gate-only resolver)
[PROD-PICK GATE] P6:  no-hit is a normal result and clears the selection
[PROD-PICK GATE] P8b: HAZARD PROVEN - after deleting index 0, id 9 still resolves to
                      index 8, which is now 'Wall_E' instead of 'Wall_W';
                      the production seam therefore stays fail-closed
```

步骤：用 gate-only resolver（`id - 1`）点中 `Wall_W`（id 9 → index 8）→ 删除 index 0
→ **同一个像素**再点，id 仍是 9 ⇒ `id - 1` 仍是 index 8，但索引 8 现在指向
`Wall_E`。**同一个像素选中了邻居**，而且没有任何错误。

这条证据同时解释了"为什么这个映射看起来是对的"：在从未删过实体的加载态下
`ID == index + 1` 恰好成立（`s_NextID` 从 1 开始，对象按装载顺序 append）。

### 5.3 待批方案（唯一满足"不增 ABI 表面"的）

| 方案 | 内容 | 代价 |
|---|---|---|
| **P-1** | 把 `reserved` 正式定义为 `entity_index`：native 在既有 `ServicePickRequestForFrame` 内用 `Scene::FindByID` 反查下标并写入；managed 解码器改为读取并校验 `index < entityCount`；ABI `v1.3 → v1.4` 让 M1 lockstep 抓住漂移 | **0 个新导出、0 个新 op、0 个新 status**、packet 仍 32B；但**是一次真实契约变更**（`reserved` 原本语义是"必须为 0"），必须版本 +1 |
| P-2 | `entity_id` 改回场景索引 | 破坏 D3-C2/U5 + `test_pick` 的"精确 `GameObject::ID`"断言 ⇒ ❌ |
| P-3 | managed 侧按创建顺序推断 | 依赖未承诺的顺序；P8b 已证删除后失效 ⇒ ❌ |

**本 slice 不擅自执行 P-1**（它改的是已冻结的 D3 ABI）。决策落地后**只改 `PickIdentity.cs`**，
gate P5 已经是它的验收用例。

---

## 6. Gate P1~P10

| Gate | 证明了什么 |
|---|---|
| **P1** | 几何前提可执行：`Stretch=None`、bitmap=800×600、控件已布局且不超过帧；mapper 的 floor / 半开区间 / 负坐标 / NaN / +Inf / 未布局语义 |
| **P2** | 受理后是 `Pending`，`PickHitsApplied` 不变，**选中不变** |
| **P3** | `Pending` 期间再点被拒绝，`PendingPickRequestId` 不变（不排队、不替换） |
| **P4** | 结果由唯一 pump 消费，`LastPickFrameId > 0`；seam 不可用时 `hit` **不**落到选中 |
| **P5** | seam 接通后 hit→选中链路可用（gate-only resolver），且恰好一次 applied hit |
| **P6** | `NoHit` 是正常结果：清选中，不是失败 |
| **P7** | 越界点击**不提交请求**、不 clamp，且有 Console 诊断 |
| **P8** | legacy `ViewportPick` 在生产后端被显式拒绝（本 gate 不使用它） |
| **P8b** | 身份危害实证（§5.2） |
| **P9** | 键盘所有权随焦点：viewport 获得/失去/重获焦点分别成立 |
| **P10** | ABI v1.3 / `PICK=29` / `OP_COUNT=30` / packet 32B 均未变 |

### 6.1 实测输出

**Debug**

```text
[PROD-PICK GATE] P1: geometry premise holds (stretch=None, source 800x600, laid-out 554x600); mapper semantics verified
[PROD-PICK GATE] P9: keyboard ownership follows viewport focus
[PROD-PICK GATE] sample: nonClear=24 candidates=10 clear=(8,8)
[PROD-PICK GATE] P2: accepted -> Pending, selection unchanged (request #1)
[PROD-PICK GATE] P3: single outstanding request preserved (#1)
[PROD-PICK GATE] P4: hit id=9 is fail-closed (no approved id->index bridge)
[PROD-PICK GATE] P5: hit id=9 at (40,296) -> selected 'Wall_W' (gate-only resolver)
[PROD-PICK GATE] P6: no-hit is a normal result and clears the selection
[PROD-PICK GATE] P8b: HAZARD PROVEN - after deleting index 0, id 9 still resolves to index 8, which is now 'Wall_E' instead of 'Wall_W'; the production seam therefore stays fail-closed
[PROD-PICK GATE] P7: out-of-frame clicks are rejected (never clamped)
[PROD-PICK GATE] ALL GREEN
prodpick exit=0
```

**Release**：同上，`prodpick exit=0`；`[prodpick] GREEN exit=0, 2.6s`。

---

## 7. 回归面（双配置）

| 类别 | Debug | Release |
|---|---|---|
| `--prodpick` | ALL GREEN, exit=0 | ALL GREEN, exit=0 |
| `--pumpowner` / `--pendingedit` / `--selstatus` | ALL GREEN, exit=0 | ALL GREEN, exit=0 |
| `--prodpath` / `--hrc2` / `--uistate` | ALL GREEN, exit=0 | — |
| `--p2frame` / `--p2ui` / `--d3managed` / `--uistatelegacy` | ALL GREEN, 自报 exit=0（linger 已知） | — |
| `test_bridge` / `test_hrc` / `test_scripting` / `test_pick` | 44 / 15 / 27 / 8 | 44 / 15 / 27 / 8 |

`assets/gp01/Main.scene` md5 仍为 `faabc37448f44197d6966708532c1ef9`（10 entities），
`game.lua` 未变 —— 本 slice 的 gate **不写 fixture**。

---

## 8. 过程中发现的两个 gate 表面缺陷（记录，未在本 slice 修）

### 8.1 flag 前缀碰撞（本次自己踩到并避开）

`Environment.CommandLine.Contains("--x")` 是**子串**匹配。最初把本 slice 的 flag
命名为 `--prodpathpick`，结果 `--prodpath` 的 handler 也被注册，两个 gate 同时跑并互相打断：

```text
[PROD-PICK GATE] P4: hit id=19 is fail-closed ...
[HRC-PROD-PATH GATE] FAIL: the service-owned pump stopped with the project (frames stayed at 2)
```

已改名为 `--prodpick`，并在入口处写明原因。

### 8.2 既有碰撞：`--uistatelegacy` ⊃ `--uistate`

同样的机制意味着 `--uistatelegacy` 会同时注册 `--uistate` 的 handler。实测该次运行
只看到 legacy 序 gate 的输出（进程先退出），**未产生假绿**，但这是同一类隐患。
属既有 gate 表面问题，**不在本 slice 范围内修改**；建议后续把 flag 匹配改成
argv 精确匹配（独立 slice）。

---

## 9. 关闭陈述

| 项 | 状态 |
|---|---|
| 点击 → canonical 像素（不传 NDC） | ✅ 完成 |
| 坐标契约经 executable assertion 冻结 | ✅ 完成（并修正过一次错误前提） |
| 请求/结果异步接通，受理 ≠ 选中 | ✅ 完成 |
| 无结果 / 越界 / 失效 / 失败各自显式 | ✅ 完成 |
| 焦点与键盘所有权 | ✅ 完成 |
| 生产路径 gate（不借 legacy 伪造） | ✅ 完成（P1~P10） |
| **点击后选中正确实体** | ⛔ Step 2 时取决于 §5 决策 → ✅ **Step 3 已关闭** |

> 结论：Slice D 的**通路**已经端到端可用且被 gate 固定；剩下的不是接线问题，
> 而是一个必须显式批准的契约变更（P-1）。在它落地前，`Hit` 保持
> `hit-unresolved` 且**不改选中** —— 宁可"点了没反应且说清原因"，
> 也不要"点了选中邻居且毫无提示"（P8b 已证）。
