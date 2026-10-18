# HRC-3 D3 Step 2 — Pick Contract Resolution

> **阶段**：UI-HRC-3 / **D3 Pick**（Step 2：Contract Resolution，**docs-only**）
> **日期**：2026-09-29
> **状态**：`D3 STEP-2 CLOSED`（U1~U5 已解决；**D3 Step 3 实现仍未批准**）
> **前置**：D3 Step 1 CLOSED（D3-C1~D3-C8 已冻结）· P1/P2 CLOSED · ABI v1.2
> **仍未开始 / 自动延续**：D3 Step 3（实现）· D4 Resize（独立）· A8

---

## 1. 本 slice 的性质

**docs-only**：只解决 Step 1 留下的 U1~U5，**不写** native / managed / shader 代码。
Step 1 的 audit 纠正了"D3 = 接线现成实现"这一认知；本 slice 确认
**D3 = 新的 production construction**，因此 contract 必须先定死，实现才允许开始。

---

## 2. U1 — implementation：**采用 `OpenGLFramebuffer::m_PickTexture`（R32I）**

**冻结**：pick target = 已存在于 **production framebuffer 结构内**的
`m_PickTexture`（`GL_COLOR_ATTACHMENT1`），格式 `R32I` / `GL_INT` / `GL_RED_INTEGER`。

证据（四条互相独立，且互相印证）：

| 证据 | 位置 |
|---|---|
| attachment 已在 production framebuffer 上，不是旁路 FBO | `OpenGLFramebuffer.cpp:131` |
| 格式已是 **R32I / GL_INT** | `OpenGLFramebuffer.cpp:130-135` |
| 4 个 ID shader 语义是 **`out int`** | `pick.frag:4`、`simple.frag:5`、`billboard.frag:3` |
| legacy pick 对 attachment 1 的 clear 已是 **signed `ClearBufferiv`** | `ViewportPanel.cpp:188-189` |

**不采用**：`PickingFBO`（R32UI，独立旁路 FBO，0 引用）·
`IDBufferPass`（RHI 非 GL，0 引用）· `ViewportPanel`（legacy / NHI-1 禁止路径）。
**不新造第四种格式。**

---

## 3. U2 — driver：**由 `Application::PumpOneFrame` 驱动**

**冻结**：driver = `Application::PumpOneFrame(float32 dt)`
（声明 `engine/include/Engine/Application.h:81`，定义 `engine/src/Core/Application.cpp:140`）。
这是 production frame 边界；managed 侧经 `EngineHost_PumpOneFrame` 驱动，
与 P2 已冻结的 `RuntimeTick` **同一入口**，故 D3 继承 P2 的 runtime-thread / frame-identity contract。

```text
UI pick request            (thread-safe 队列；UI thread 不做 GL)
    ↓
existing runtime pump     Application::PumpOneFrame
    ↓
production frame:
    ├─ normal scene pass
    └─ pick-ID pass        ← 仅当存在 pending request
          ↓
      read ID               (runtime thread)
          ↓
      result { frame_id, entity_id | no-hit }
```

**不**把 pick 交回 `ViewportPanel`；**不**建第二个 pump；**不**建 pick thread。

---

## 4. U3 — `location = 1` isolation：**独立 pick shader / pass**

**冻结**：dedicated pick pass，显式声明
`layout(location = 1) out int EntityID;` 并写入该对象的**真实** `GameObject::ID`。

- **不**与 deferred / normal shader 共用同一个 fragment-output contract。
- `deferred_geom.frag:20` 的 `location = 1 → g_NormalRgh`（RGB10A2 颜色）与
  pick 的 `location = 1 → EntityID`（整数）属**不同 render pass**，
  而非同一 shader / interface 同时承担两个 incompatible attachment。
- **明确不为 D3 修改 `lit.frag`。** 其 `u_EntityID = -1` 默认值
  （`lit.frag:10`）**不是** D3 implementation prerequisite：
  在 U4 冻结的 `-1` sentinel 下，该默认值恰好落回 no-hit 语义，
  **不再构成碰撞**（见 §9 的诚实修正）。

---

## 5. U4 — coordinate semantics：**reject，never clamp**

**冻结**：

- `x/y` 越界 → `InvalidCoordinate` / `OutsideViewport` 的 **no-hit / invalid result**；
  **禁止静默 clamp**（clamp 会把越界点击变成边缘假命中）。
- 只定义**一个** Y 转换公式；**不兼容**树内两个相差一像素的旧实现
  （`PickingFBO.cpp:140` 的 `height-1-y` vs `ViewportPanel.cpp:398` 的 `height-y`）。
- **公式常数本 slice 不冻结**：必须先确定 Avalonia / GL 坐标约定，
  并用**一个已知像素做 executable assertion** 之后，才冻结为 `height-1-y` 或其他。
  在该 assertion 存在之前，任何常数选择都只是猜测。

---

## 6. U5 — entity identity：**复用 `GameObject::ID`**（read-only audit 结论）

不引入 `PickHandle`，不造第二身份系统。逐条判定 D3 所需的四项要求：

| 要求 | 判定 | 证据 |
|---|---|---|
| unique | ✓（附一条约束） | `GetNextID()`: `static uint32 s_NextID = 1; return s_NextID++;`（`GameObject.h:25`）进程内单调；仅 2 个 ctor 且都在初始化列表设 `m_ID(GetNextID())` |
| nonzero | ✓ | 从 **1** 开始；`m_ID = 0`（`GameObject.h:244`）只是默认成员初值，被两个 ctor 覆盖 → 活对象 ID **≥ 1** |
| current-session identity | ✓ | 进程级 `static`，不重置；pick 结果本身是 frame-scoped，跨 session 无需稳定 |
| 可从 pick result 可靠解析 | ✓ | `Scene::FindByID(uint32)` public（`Scene.h:109-110`），`findByIDRecursive` 比对 `GetID() == id`，未命中返回 `nullptr`；唯一真实消费者 `EngineEditor.cpp:216` 已显式处理未命中 |

**额外证据**：`m_ID` 声明处注释（`GameObject.h:244`）本身写明
"全局唯一标识，编辑器 Picking 相关" —— 该 ID **原本就是为 picking 设计的**，
复用它是延续既有设计，而不是新增身份层。

### 必须一并冻结的已知边界（非阻塞，但不得被"唯一"二字掩盖）

1. **无拷贝构造/拷贝赋值声明** ⇒ 编译器会**隐式生成**拷贝构造，
   按值拷贝会**复制 `m_ID`** ⇒ uniqueness 依赖"GameObject 从不被按值拷贝"。
   现状未被违反（scene 以 `std::shared_ptr<GameObject>` 持有，见 `findByIDRecursive` 签名）。
   **D3 不得引入按值拷贝**；后续 slice 可考虑显式 delete 拷贝构造。
2. `s_NextID` 是 `uint32` ⇒ 理论上 2³² 次分配后**回绕**。
   实际不可达；作为**已知上限**记录，**不**声称"绝对唯一"。
3. `FindByID` 是**线性递归**搜索 ⇒ 单次点击 O(n·depth) 可接受，
   **禁止**在每帧调用。
4. `findByIDRecursive` 递归进 children ⇒ ID **与层级无关**地解析（有利）。
5. entity 在 frame 与结果之间被删除 ⇒ `FindByID` 返回 `nullptr`
   ⇒ D3-C6 的 `StaleEntity` 由此**可判定**，无需额外机制。

---

## 7. 冻结后的 pick contract（最终）

```text
Pick target:    R32I / existing m_PickTexture (GL_COLOR_ATTACHMENT1)
Sentinel:       -1                       (background / no-hit)
Valid ID:       >= 1
Reserved:       0                        (invalid / unset，不得作为 entity ID)
Driver:         Application::PumpOneFrame (existing runtime pump)
Thread:         runtime thread
Frame identity: result carries owning frame_id
Shader:         dedicated pick pass (out int EntityID @ location = 1)
Y semantics:    one explicit formula   (常数待 executable assertion 后冻结)
OOB:            reject, never clamp

Result:
  Hit(entity_id, frame_id)
  NoHit(frame_id)
  InvalidCoordinate / OutsideViewport
  RuntimeNotReady / disposed / failure
```

sentinel 取 **-1**（而非 Step 1 暂定的 0）的收益是**三方证据自然收敛**：
`out int` shader 语义 · `R32I` attachment · legacy 已有 signed `-1` clear ·
`EngineEditor.cpp:214` 的 `if (entityId > 0)`。同时 0 被保留为 reserved，
`>= 1` 的 valid 区间与 U5 的 `s_NextID` 从 1 开始**天然对齐**。

---

## 8. 废止条款（实现前必须执行，否则 contract 自相矛盾）

1. **`ReadPixelID` 的「0 = failure / no-hit」语义废止** → 改为结构化 result。
   否则 D3-C6 永远无法干净闭合。
2. **`OpenGLFramebuffer.cpp:247` 的 `std::max(0, pixelValue)` 必须移除。**
   它会把 `-1` sentinel **clamp 成 0**，直接摧毁本次冻结的 sentinel。
3. **pick pass 必须显式 clear attachment 1 到 `-1`**
   （`glClearBufferiv(GL_COLOR, 1, &clearId)`，`clearId = -1`），
   **不得依赖普通 `glClear`**（那会清成 0；0 是 reserved 而非 sentinel，
   未命中将变成**假命中**）。
   树内已有该手法的先例：`ViewportPanel.cpp:188-189`。

---

## 9. 对 Step 1 结论的诚实修正

Step 1 的 U4 记为「`lit.frag` 的 `u_EntityID = -1` 默认值会写入 `0xFFFFFFFF`，
与 sentinel 0 冲突，需修正」。在本 slice 冻结 **sentinel = -1** 之后，
该 `-1` 默认值恰好等于 no-hit sentinel，**不再构成冲突**。
因此 **U4 不再是待修正项**，而 D3 也**不需要**为此改动 `lit.frag`。
记录此点是为避免下一位执行者按 Step 1 的旧描述去动 `lit.frag`。

---

## 10. 本 slice 边界

- **零代码**：未修改任何 runtime / native / shader / managed / ABI / I2。
- `test_bridge` 仍 44/44，**I2 仍 243**，ABI 仍 **v1.2**，
  `test_hrc` 15/15，`test_scripting` 27/27；P2 standing runner 不受影响。
- **D3 Step 3（实现）未批准**，本 slice 不预设其实现方式。
- 实现期 gate 必须自带：ID pass 真实写入 · frame identity 可证 ·
  no-hit ≠ failure · OOB reject · **sentinel 存活（-1 未被 clamp）**。
