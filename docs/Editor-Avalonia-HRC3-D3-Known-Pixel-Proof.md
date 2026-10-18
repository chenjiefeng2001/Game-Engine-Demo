# HRC-3 D3 Step 3.1 — Native Known-Pixel Proof

> **阶段**：UI-HRC-3 / **D3 Pick**（Step 3.1：native known-pixel proof）
> **日期**：2026-09-29
> **状态**：`D3 STEP-3.1 CLOSED`（native 空间语义已由可执行证据闭合；**D3 Step 4 / ABI 未批准**）
> **前置**：D3 Step 1 CLOSED · D3 Step 2 CLOSED · D3 Step 3 structural GREEN · P1/P2 CLOSED
> **仍未开始 / 自动延续**：D3 managed 链接与 ABI mini-contract · D4 Resize（独立）· A8

---

## 1. 为什么必须要有这一步

D3 Step 3 的 structural 实现只能证明「pick pass **执行了**、且没有破坏 P2 的 color path」。
它**不能**证明「ID 落在了正确的像素上」—— 一个整体偏移一行的实现同样会
`resources ready` + `GL clean` + 颜色帧完全正常。

**已知像素命中证明是当时唯一真正的 correctness gap**，本 slice 就是来填它的。

---

## 2. 关键设计：已知像素来自**真实帧内容**，不复制投影矩阵

夹具先从 **color attachment（attachment 0）实测**精灵的屏幕包围盒，再去
ID attachment 取样。

这样做的原因：如果测试自己重算一遍投影数学去"猜"精灵在哪，它就变成了
color pass 的**第二份实现**——而两份实现一旦漂移，测试会跟着一起错，
**恰好测不出真正的缺陷**。从真实帧里量出来，才是对 color pass 的独立交叉验证，
也正是 D3-C1 关心的"两个 pass 不得错位"。

夹具还刻意**不链接 EditorBridge**：这是 native-only 证明，不经 C ABI，
D3 的空间语义不会被 ABI 层顺手兜住或掩盖。

---

## 3. 结论：8/8 GREEN（Debug + Release）

| 用例 | 证明内容 | 对应 contract |
|---|---|---|
| `KnownInteriorPixel_ReturnsExactEntityId` | 已知像素 → **精确** `GameObject::ID`（两个精灵分别命中） | D3-C1 / D3-C2 |
| `CoordinateMapping_DiscriminatesHeightMinusOneVsHeight` | 坐标公式由**实测**判定并冻结 | **D3-C3** |
| `BackgroundPixel_ReadsBackSentinelMinusOne` | 背景 → 原始 GL 读取**精确 -1** | D3-C2 / 废止条款 3 |
| `ZeroIsNeverAValidHit` | attachment 内**不存在** 0 | 废止条款 4 |
| `OutOfRange_RejectsWithoutClamping` | 越界 → `OutsideViewport`，**不返回任何实体** | D3-C3 / 废止条款 5 |
| `NonFiniteCoordinate_RejectsAsInvalid` | NaN/±Inf → `InvalidCoordinate` | D3-C6 |
| `MissingPickTexture_ReportsNotReadyNotNoHit` | 无 pick 纹理 → `NotReady`（**≠ NoHit**） | D3-C6 |
| `ProductionPickPass_ActuallyWroteIds` | 至少一行携带真实 ID，排除"资源就绪但没写" | D3-C1 |

---

## 4. 本 slice 查出的三个真实缺陷（都不是夹具问题）

### 4.1 `m_GL` 在 production 路径下为 null → pick pass **静默跳过**

`Application::m_GL` 此前**只**在 `InitViewportFBO()` 里赋值，而该函数依赖非 headless 的
`m_Window`。EngineHost 用的是 **headless** `Application`（无窗口），因此 `m_GL` 恒为
`nullptr` —— 我最初加的 `if (m_GL && ...)` 守卫让整个 pick pass **静默不执行**：
不报错、不写任何 ID。

> 这正是 D3-C1 要排除的「看起来正常、其实什么都没做」。
> 修复：从传入的 `IRenderContext&` 显式解析 GL context，并在解析失败时**显式报错**。

### 4.2 坐标公式实测为 `H - y`，比冻结契约**差一行**

自校准探针（沿中列找第一个 `Hit` 的 canonical y，再反推实现）测出实现等价于
`glY = H - canonicalY`，而 D3-C3 冻结的是 `H - 1 - canonicalY`。

后果不是风格问题：canonical row 0 **不可达**，且每一个采样都向上偏移一行 ——
pick 结果与鼠标实际所在像素**系统性错位一行**。

> 修复：`py = H - 1 - y`。
> 判别力自检：两个候选公式在该边界上**恰好一个**成立，否则夹具会自报无法定案。

### 4.3 每帧无条件跑 pick pass 会让 production 挂起

在修正 4.1（pick pass 真的开始执行）之后，P2 standing gate 两个场景**双双 timeout**，
直跑 `--p2frame` **超过一小时未返回**。颜色证据仍正确（`nonClear=7296`），但进程挂住。

- 已排除：不是性能问题（gate 只跑 3 帧；`Flush()` 只是 re-upload + draw）。
- **本 slice 未定位根因**，不做也不允许做任何重试/延迟掩盖。
- 处理：pick pass 改为**显式 opt-in，默认关闭**。这同时也是 U2 冻结语义的本意 ——
  「pick-ID pass **仅当存在 pending request** 时运行」，而**当前根本还没有请求通道**
  （那属于后续 ABI mini-contract）。

修正后 P2 standing gate 恢复 **ALL GREEN（25s，`nonClear=7296` 不变）**。

---

## 5. 夹具自身的两个 bug（记录以免重蹈）

1. **UAF**：`Texture` 由 `Application` 的 `StackAllocator` 分配。若先销毁 `Application`
   （释放 arena）再释放 `shared_ptr<Texture>`，引用计数会落在已释放内存上
   （ASan: `heap-use-after-free`）。**顺序必须是** `Scene` → `Texture` → `Application`。
2. **GLFW 持久化上下文需要先声明 main thread**。经由 C ABI 时这一步是
   `EditorBridge` 的副作用（`capi.cpp:184`）；native-only 夹具绕过 C ABI，
   因此必须显式 `RegisterGlfwOwnerThread()`，否则 `AcquireGlfwLifetime` 直接拒绝。

> 第 2 条本身就是一个结论：**C ABI 之前在替 native 路径承担线程声明职责。**

---

## 6. I2 基线变化（显式更新，不假装没变）

新增 native test inventory，因此 I2 **必须**重算：

| Target | 变化 |
|---|---|
| `test_bridge` | 42 → **44**（此前本行陈旧，实际 discovery 一直是 44；本次一并修正，使表内合计与总数自洽） |
| `test_pick` | **新增 8** |
| **I2 合计** | 243 → **251** |

其余目标实测未变：`test_scripting` 27 · `test_physics` 54 · `test_renderer` 9 ·
`test_content` 42 · `test_job` 4 · `test_e2e` 6 · `test_gp01` 22 · `test_hrc` 15 ·
`test_core` 20。

---

## 7. 冻结基线（未回退）

- `test_pick` **8/8**（Debug + Release）
- `test_bridge` **44/44**（Debug + Release）· `test_hrc` **15/15** ·
  `test_scripting` **27/27**
- **ABI v1.2 不变**（本 slice 未新增 export / op / packet）
- **I2 251**（243 + 8）
- P2 standing runner **ALL GREEN**，`nonClear=7296` 不变
- D4 Resize 未进入 · A8 未进入

---

## 8. 未决项（进入下一 slice 前必须先答）

1. **4.3 的挂起根因未定位。** 在它被解释清楚之前，pick pass **不得**在 production
   默认开启。按需触发属于后续 D3 ABI mini-contract 的职责。
2. **D3-C1 的 EngineHost 侧证明尚未完成。** 本夹具证明的是「pick pass 语义正确」，
   但它自建 frame，**不是** EngineHost 的 production frame。managed 请求 → native
   pick 的链路（及其必然带来的 ABI transport / result packet）仍需先冻结
   **D3 ABI mini-contract**，不得在 pick implementation 里夹带。
3. D3 managed/UI 消费（点击 → selection）未进入。
