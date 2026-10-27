# HRC-3 P1 — 实现记录（**P1 CLOSED**，2026-09-28）

> **阶段**：UI-HRC-3 / P1 native implementation
> **日期**：2026-09-28
> **状态**：✅ **P1 CLOSED** — B1 CLOSED / B2(a)+(c) CLOSED / B2 测试隔离根因 CLOSED /
>   生产渲染门控 **ON** / P1 验收用例 **ACTIVE** / `test_bridge` **42/42**（Debug+Release，各 20 次重复全绿）
> **契约**：`docs/Editor-Avalonia-HRC3-P1P2-Contract.md`（P1-H1~H7 + V1-a）
> **裁决报告与关闭矩阵**：`docs/Editor-Avalonia-HRC3-P1-Investigation-Report.md`
>   —— **结论以该报告为准**；本文件保留逐条原始实验数值与**被撤回的结论**，不做事后改写。
> **基线**：`docs/Evidence-Baseline.md` 已记录激活前 **41**（保留）与激活后 **42** / I2 **241**。
> **未触碰**：`--prodpath`、P2 Fetch op / ABI v1.2、picker、resize、A8。

---

## 1. 已落地且可复用的结构（native-only，零 C ABI 变更）

| # | 内容 | 位置 | 状态 |
|---|---|---|---|
| 1 | **production-owned render entry**：`InitProductionRenderResources(IRenderContext&)` + `RenderProductionFrame(IRenderContext&, Scene&, w, h)`。含相机按 sprite 世界坐标 AABB 取景（含 10% 边距与宽高比补齐） | `Application.h/.cpp` | 编译 + 运行通过 |
| 2 | **presentation target**：`ENGINE_HOST_PRESENTATION_WIDTH/HEIGHT`（800×600）单一来源常量（`capi.h`，编译期常量，**非 ABI 变更**）+ EngineHost 独立 FBO（与 64×64 探针 FBO 完全分离，G2P-01 证据零改动） | `capi.h`、`EngineHost.{h,cpp}` | 编译 + 运行通过 |
| 3 | **frame 编排**：既有 frame 内 bind → clear → viewport → render → unbind；硬失败走 typed failure（`GL_FAILED` / `STAGE_FBO`）+ emergency stop | `EngineHost.cpp` | 编译通过 |
| 4 | **P1 测试 seam**：`PresentationTargetWidth/Height`、`LastProductionRenderableCount`、`ReadPresentationPixels`（走 runtime 线程命令队列，GL 访问纪律与 P2-H4 同源）。以 `ENGINE_HOST_P1_TEST_SEAM` 导出 **C++ 符号**，非 C ABI 入口 | `EngineHost.{h,cpp}` | 编译 + 链接通过 |
| 5 | **真实缺陷修复**：场景反序列化只保留 sprite GUID、**不绑纹理** ⇒ `RenderCommand::texture` 全 null ⇒ `SceneRenderer::Render` 跳过全部 sprite。新增幂等 `EditorSession::BindEditSceneSpriteTextures()`（只读 GUID、不改语义、不发事件） | `EditorSession.{h,cpp}` | 生效：textured 9/9 |
| 6 | **P1 native contract test**：`renderables > 0` 前置断言（不足即报 insufficient scene content）+ 非背景像素计数 + **颜色直方图诊断** + 二次读回**确定性**比对 | `tests/test_bridge/test_editor_capi.cpp` | 失败（见 §3.1） |
| 7 | 测试白色盒 include（`bridge/src`）| `tests/CMakeLists.txt` | 通过 |

**语义修正（我自己的实现缺陷，已改）**：初版把「没有可渲染内容」做成 **frame 失败**，结果破坏了
HRC-2 冻结的 pump/frame 契约（`RuntimeSeamProofThreeCycles` 的 `PumpOneFrame` 变成 -1）。
现分层为：**生产侧**「没内容 ⇒ 什么都不画、返回成功、计数 0」；**证据侧**由 gate 的
`renderables > 0` 前置断言把「clear-only」判为不通过。运行态（play 期间编辑场景被换出）
没有 renderable 是合法状态，不该让帧失败。

---

## 2. 撤回的实验（保持 diff 诚实）

- 曾在 `OpenGLSpriteBatch::SubmitBatch()` 加管线状态设置（blend/cull/depth/scissor/color-mask），
  **实测无效**（不是根因）→ 已完整撤回，不留在 diff 里。

---

## 3. 两个 BLOCKER（实测，非推断）

### 3.1 V1-a 内容路径不产生任何片元

逐步实测（全部有日志证据）：

| 观测项 | 值 | 含义 |
|---|---|---|
| 探针 FBO | `glError=0x0` | **没有任何 GL 错误** |
| presentation 中心像素 | `(13,13,18,255)` | 恰好是 clear color |
| 全帧直方图 | `#0D0D12 = 480000`（= 全部像素） | 帧里只有背景色，**一个 sprite 都没画出来** |
| 命令收集 | `commands=9 textured=9` | 纹理已正确绑定（§1.5 修复生效） |
| 批处理 | `batchSprites=4` | 绘制**确实提交**了（9 命令 / 3 纹理 ⇒ 4 次 batch） |
| 几何 | `firstPos=(0,0) scale=1`，相机 `(-10.45,10.45)×(-7.84,7.84)` | 不退化、不在视锥外 |
| 着色器 | 编译/链接无任何错误日志 | program 有效 |

即：**合法绘制 + 零 GL 错误 + 零片元**。这是"从未执行过的代码"的典型静默失败形态 ——
`SceneRenderer` 与 `OpenGLSpriteBatch` 在本仓库**从未被实例化过**（与 `PickingFBO` 同类死代码：
全仓唯一 `SetSceneRenderCallback` 注册在 legacy imgui editor，且其 injector 从未被调用）。

### 3.2 渲染路径会污染下一帧的探针读回（动到 HRC-2 冻结证据）

把 §1.3 的渲染块整体关闭 → `test_bridge` **41/41 全绿**；打开 → `RuntimeSeamProofThreeCycles`
在 `PumpOneFrame` 上拿到 -1（下一帧 64×64 探针的 clear-color 校验失败）。
说明该路径**泄漏 GL 状态**，使后续帧的既有探针语义被破坏。

---

## 4. 为什么必须停下来裁决

我原本汇报的 P1 范围（headless shader init + Application-owned render step + 接线 `SceneRenderer`）
**低估了实际工作量**：`SceneRenderer` / `OpenGLSpriteBatch` 是从未跑过的 2D sprite 渲染器，
把它在 headless 下跑通属于"把引擎死代码救活"，是一个独立子项目，且 `IRenderContext`
抽象**不暴露**管线状态控制（无 cull/depth/blend/scissor setter），修法只能在
`OpenGLSpriteBatch` / `SceneRenderer` 内部或扩展抽象 —— 两者都超出已 ratify 的 V1-a 文字范围。

继续 unilateral 地挖，会重复"先假设、后发现前提不成立"的模式。可选：

| 选项 | 内容 | 代价 |
|---|---|---|
| **A（建议）** | 独立子 slice **P1.1：修复 sprite 渲染管线**（在 `OpenGLSpriteBatch`/`SceneRenderer` 内部确立管线状态 + 定位静默丢片元的根因），自带证据；P1 结构层保持现状等它 | 工期不定；只动引擎渲染内部，不碰 C ABI / HRC-2 冻结面 |
| **B** | 改 v1 内容为引擎 3D 路径（`3d_lit.*` + mesh 渲染） | `tests/test_renderer` 只有 compute/RenderGraph 用例，**3D 场景渲染同样无证明**，大概率更大 |
| **C** | P1 按"结构 + typed 失败 + 测试 seam"收口，**内容断言明确后置** | 缩小已 ratify 的验收范围，需你批准；好处是今天就能把结构层与 seam 固化 |

已回退全部实验性改动；`OpenGLSpriteBatch` 与探针路径均保持原样。

---

## 5. P1.1 分层定位结果（2026-09-28）

方法：按你指定的链条逐层做**单假设**实验，每层一旦证明无问题即冻结，不重复测量。
链条：`program → VAO/VBO → vertex attributes → texture sampler → blend → framebuffer attachment → viewport/scissor → draw`

### 5.1 已冻结为"正确"的层（均有正面证据）

| 层 | 证据 |
|---|---|
| program | `Resource::IsFailed()==false && IsLoaded()==true`（显式检查通过）；draw 时 `GL_CURRENT_PROGRAM=3` |
| VAO/VBO/EBO | `vao=1 vbo=1 ebo=2`（非 0，构造成功） |
| index | `SetCapacity` 在 ctor 内把索引上传到 EBO（`OpenGLSpriteBatch.cpp:59-63`） |
| vertex attributes | `SpriteVertex` = 8 float / 32B 无 padding；属性 offset 0/8/16 与 `sprite_batch.vert` 的 location 0/1/2 一致 |
| geometry / camera | quad 非退化（pos (0,0)、scale 1）；相机 extent 有限且覆盖内容（±10.45 × ±7.84） |
| FBO 线程归属 | `InitializeRuntimeResources` 运行在 **runtime 线程的 bootstrap command** 内（`EngineHost.cpp:796`），`IsOwnerThread()` 成立（我曾误判此处，已用证据推翻） |
| viewport | `GL_VIEWPORT=(0,0,800,600)` |
| scissor | `GL_SCISSOR_TEST=0` |
| blend / cull / depth / stencil | 全部 `0`；`COLOR_WRITEMASK=0xF`、`DEPTH_WRITEMASK=1`、`STENCIL_WRITEMASK=0xFFFFFFFF` |
| draw 确实提交 | `batchSprites=4`（9 命令 / 3 纹理 ⇒ 4 次 batch） |
| 采样层 | 把 `sprite_batch.frag` 临时改成常量 `vec4(1,0,1,1)`：**仍然 0 片元** ⇒ 与纹理采样无关 |
| render 自身无 GL 错误 | draw 后 `glGetError()==0`（诊断中出现的 `0x502` 来自我自己的读法：把整数 pick attachment 当 RGBA 读） |
| 两个 attachment 都没内容 | att0 = clear color、att1 = 全 0 ⇒ 不是"画错 attachment"，是**根本没画** |

### 5.2 B1 未定位（唯一剩余未知）

输入全部正确、draw 合法、常量色着色器也无片元、两个 GL 错误都没有 —— 失效点落在
`OpenGLSpriteBatch::SubmitBatch()` 内部那一次 `glDrawElements` 上（这条路径在本仓库从未执行过）。
把它隔离出来需要**第二条独立的诊断渲染路径**（自带最小 program + `glDrawArrays`，在有 GL 访问权的
`EngineHost` 侧发一次），用来判定"FBO/viewport/program 这条路本身能否出片元"。这是下一步的**唯一**
有界动作。

### 5.3 B2：我引入的回归（已定位并已收口）

在把渲染真正接进每帧的过程中，我引入了**破坏 HRC-2 冻结证据**的回归，现已定位并修掉：

- 现象：`test_bridge` 由 **41/41** 退化为 **7~8 项失败**，多台 host 连续创建时后续 host 报
  `failed to initialize GLFW`；`RuntimeSeamProofThreeCycles` 的 `PumpOneFrame` 变 -1。
- 定位：不是每帧渲染调用本身，而是**bootstrap 期创建的 GL 资源**
  （sprite-batch shader program + VAO/VBO/EBO）。它们的析构**无条件删除 GL name**，
  而 host 拆除时既不保证 current context、也不保证在创建线程上 → 驱动状态被破坏。
  逐层排除记录：关闭每帧渲染仍失败 → 关闭 bootstrap 期 `InitProductionRenderResources` 后恢复。
- 收口（当前代码状态）：
  - `kEnableProductionSceneRender = false`：每帧渲染**显式关闭**，并写明 B1/B2 原因；
  - bootstrap **不再创建** sprite-batch GL 资源（渲染入口 / presentation target / 测试 seam 全部保留）；
  - P1 验收用例改名为 `DISABLED_ProductionSceneFrameRendersSessionContent`，
    注释写明它是 P1 验收判据、不得用 clear-only 或 default quad 伪造通过；
  - 结果：**Debug + Release 均 41/41 PASSED、exit 0、1 DISABLED**；`test_hrc` 15/15、
    `test_scripting` 27/27（两配置）—— HRC-2 冻结证据完好，C ABI 未变。

### 5.4 已撤回的实验（diff 保持诚实）

- `OpenGLSpriteBatch::SubmitBatch` 的管线状态设置（blend/cull/depth/scissor/color-mask）：**实测无效**，已 `git checkout` 还原。
- `assets/shaders/sprite_batch.frag` 常量色补丁：**已按字节还原**（`git diff` 为空）。
- `OpenGLSpriteBatch.h` 的临时 `DebugVao/Vbo/Ebo` 访问器：已还原。
- `Application.cpp` / `EngineHost.cpp` 的逐帧诊断日志：已移除（保留 shader 状态检查，它是 P1-H7 的
  真实修复：`CreateShader` 失败时不打日志，必须显式查 `IsFailed()`）。

### 5.5 决定性诊断：presentation FBO 侧完全正常（B1 收窄到 sprite batch）

按授权只做**一次**有界诊断，问题是唯一的：「同一个 presentation FBO + runtime GL context +
800×600 viewport，能否光栅化出任何片元？」

实现（临时、已删除）：runtime 线程 + `MakeRuntimeContextCurrent` → 绑定**同一个**
presentation FBO → 同一 800×600 viewport → 内联 GLSL（vertex 用 `gl_VertexID` 生成三角形，
fragment 常量品红）→ 最小 VAO → `glDrawArrays(GL_TRIANGLES, 0, 3)` → `glReadPixels` 统计品红像素
→ **在 context 仍 current 时**删除 program/VAO/shader。

结果：

```text
[DIAG] rc=0  magenta=86400  glError=0x0  target=800x600
```

`86400 = 480×360÷2`，与 NDC 跨度 0.6 的三角形在 800×600 上的理论面积**精确吻合**
（480 = 0.6×800，360 = 0.6×600，三角形取一半）。

**判定（冻结，不再重复测）**：

| 结论 | 依据 |
|---|---|
| presentation FBO 可正常光栅化 | 品红片元精确出现 |
| runtime GL context / make-current 正常 | 同上 |
| 800×600 viewport 正常 | 同上 |
| 内联 program 编译/链接/使用正常 | `glError=0x0` |
| `glDrawArrays` + 最小 VAO 正常 | 同上 |
| 诊断本身不污染状态 | 全套 `test_bridge` 42/42 通过、exit 0 |

⇒ **B1 从"渲染基础设施"收窄到 sprite batch 专属路径**
（`OpenGLSpriteBatch::SubmitBatch` 的那一次 `glDrawElements`，或喂给它的顶点/索引数据）。
基础设施侧（context / FBO / viewport / program / 光栅化）**已证明无问题**，不再回头检查。

### 5.6 诊断清理状态

临时 seam（`EngineHost::RunDiagnosticTriangle`）与临时用例（`TEMP_DiagnosticTriangleRasterizes`）
**已完整删除**；`assets/`、`OpenGLSpriteBatch.*` 未被本次诊断改动。
清理后复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（Debug）。

### 5.7 A2 结果（冻结）：payload 取到了，但诊断自身 setup 抛 INVALID_OPERATION ⇒ 问题**未回答**

按授权只做 A2：把**真实 SpriteBatch 那一帧的顶点/索引数据**送进 A1 已证明可用的诊断光栅路径
（同一 context / 同一 800×600 FBO / 同一 viewport / 同一常量品红 program / 同一最小 VAO 策略），
不实例化 `OpenGLSpriteBatch`，只复制它会产生的 payload。

结果（Debug）：

```text
[DIAG-A2] rc=0 sprites=9 magenta=0 afterSetup=0x502 afterDraw=0x0 afterRead=0x0
```

| 观测 | 判定 |
|---|---|
| `sprites=9` | **CPU 侧 payload 提取成功**：9 个真实 sprite → 36 顶点 / 54 索引，索引式与 `GenerateIndices` 逐字一致 |
| `afterSetup=0x502` | `GL_INVALID_OPERATION`，且**在排空此前遗留错误之后仍然出现** ⇒ 由诊断自己的 VBO/EBO/VAO/属性设置块抛出 |
| `afterDraw=0x0` / `afterRead=0x0` | draw 与 readback 本身干净 |
| `magenta=0` | **但 draw 从未合法发生** |

**因此 A2 的问题（"真实 payload 能否出片元"）是"未回答"，不是"不能"。**
按决策矩阵第 3 行处理：**捕获错误与状态、冻结证据、停止**（未做任何修补）。

**一个必须诚实指出的线索**：出错的设置块是本次为镜像 `OpenGLSpriteBatch` ctor 而**新写**的代码
（`GenVertexArrays` / `GenBuffers` / `BindVertexArray` / `BindBuffer` ×2 / `BufferData` ×2 /
`EnableVertexAttribArray` ×3 / `VertexAttribPointer` ×3 / `GetUniformLocation` / `UniformMatrix4fv`）。
生产 `OpenGLSpriteBatch` ctor 执行的是**高度相似**的设置序列。若同类调用在生产侧同样非法，
那本身就可能是 B1 的直接解释 —— 但这是**待验证的假设**，不是已证结论，本轮不予修补。

清理状态：A2 临时 seam（`RunDiagnosticBatchPayload`）与临时用例（`TEMP_DiagnosticBatchPayload`）
已完整删除；`assets/`、`OpenGLSpriteBatch.*` 未被本轮改动。
清理后复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（Debug + Release）。

> 一次运行中 `EditorBridgeTest.F2ColliderNegativePaths` 偶发失败（该用例写 scratch 文件，
> 日志为 `set component: write failed for 'shape'`），单独复跑与整轮复跑均通过 ⇒ 判定为
> 仓库既有的 scratch/文件系统 flake，与 P1 改动无关（它是 legacy EditorSession 用例，不经过 EngineHost）。

### 5.8 A3 结果（冻结）：第一个非法调用 = 步骤 16 `glUniformMatrix4fv`；**根因是诊断自身漏了 program 激活**

drain → 单一逻辑步骤 → `glGetError()`，逐步记录（Debug）：

```text
[A3] rc=0 control=86400 payload=-1
[A3] step 0-program            err=0x0
[A3] step 1-genVAO              err=0x0
[A3] step 2-bindVAO             err=0x0
[A3] step 3-genVBO              err=0x0
[A3] step 4-bindVBO             err=0x0
[A3] step 5-genEBO              err=0x0
[A3] step 6-bindEBO             err=0x0
[A3] step 7-vertexBufferData    err=0x0
[A3] step 8-indexBufferData     err=0x0
[A3] step 9-attrib0Enable       err=0x0
[A3] step 10-attrib0Pointer     err=0x0
[A3] step 11-attrib1Enable      err=0x0
[A3] step 12-attrib1Pointer     err=0x0
[A3] step 13-attrib2Enable      err=0x0
[A3] step 14-attrib2Pointer     err=0x0
[A3] step 15-getUniformLoc      err=0x0
[A3] step 16-uniformMatrix4fv   err=0x502   ← 第一个非法调用
```

- **control = 86400**（A1 三角形仍精确命中）⇒ 本实验没有改变 context 假设。
- 步骤 0–15 **全部干净**；第一个非法调用是 **`glUniformMatrix4fv`**（`GL_INVALID_OPERATION`）。
- `payload = -1`：因存在脏步骤，代码按设计**没有执行 draw** ⇒ A2 仍未回答。

**只读对比（按指令，先读后判）**：

| 事实 | 出处 |
|---|---|
| `OpenGLShader::Bind()` 就是 `glUseProgram(m_RendererID)` | `OpenGLShader.cpp:250` |
| `SceneRenderer::Render` **先** `m_Shader->Bind()`（L41）**再** `m_Shader->SetMat4(...)`（L42） | `SceneRenderer.cpp:41-42` |
| `OpenGLShader::SetMat4` 与我的诊断写法一致：`GetUniformLocation(prog,name)` → `UniformMatrix4fv(loc,1,GL_FALSE,data)` | `OpenGLShader.cpp:253-256` |

**结论（严格限定，不外推）**：

1. 有效结论仅为：*"镜像出的设置序列在该 runtime context 下包含一个非法 OpenGL 操作，
   定位到 `glUniformMatrix4fv`。"*
2. **不**得出"生产 `OpenGLSpriteBatch` 有同样缺陷"——恰恰相反：**生产路径先绑定 shader 再设 uniform，是正确的**。
3. 真正原因是**诊断自身**在 control 段结束时 `UseProgram(0)`，随后拿 `prog` 的 location 去设 uniform；
   此时活动程序是 0，`UniformMatrix4fv` 按 spec 校验失败 → `0x502`。
4. 因此 **A2 的 `0x502` 是诊断代码的产物，B1 未被解释**；A2 的问题现在只差"补一次 `UseProgram(prog)`"即可回答。

清理状态：A3 临时 seam（`RunDiagnosticSetupBisect`）与临时用例已完整删除；
`assets/`、`OpenGLSpriteBatch.*` 未被本轮改动。清理后复核：
`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）。

### 5.9 A2 重跑结果（冻结）：**payload 可渲染 —— B1 收窄到 `OpenGLSpriteBatch` 自身的 GL 处理**

唯一改动：`glUniformMatrix4fv` 之前加 `glUseProgram(prog)`（与生产 `SceneRenderer` 同序）。
其余与冻结的 A2 实验完全相同，并保留 A1 control。

```text
[A2R] rc=0 control=86400 payload=91656 afterSetup=0x0 afterDraw=0x0 afterRead=0x0
```

| 观测 | 判定 |
|---|---|
| `control = 86400` | 与 A1 逐字一致 ⇒ 实验有效，context 假设未变 |
| `payload = 91656` | ~~**真实 9-sprite / 36-vertex / 54-index payload 产生了 91,656 个片元**~~ ⚠️ **该数字已被 A6 修正，见 §5.11：91,656 含 A1 control 三角形的残留，payload 自身约 6,552。结论方向不变（payload 可渲染），但数字不可再作为基线** |
| `afterSetup / afterDraw / afterRead = 0x0` | 全程无 GL 错误 |

**结论（按矩阵第 1 行）**：payload 本身**可渲染**。因此 B1 **不在**以下任何一项：
scene payload、索引式、顶点坐标数据、正交取景、顶点属性布局、EBO 索引区间、VAO/VBO 设置序列。

**B1 现在被收窄到 `OpenGLSpriteBatch` / `SceneRenderer` 自身与"payload → 真实 batch draw"之间的差异**，
即以下候选（尚未验证，不在此下结论）：

| # | 候选差异 | 备注 |
|---|---|---|
| C1 | **管线状态**：诊断在 draw 前显式关闭 scissor/cull/depth/blend；`OpenGLSpriteBatch` 自己不设置任何状态 | 之前"给 batch 加管线状态"的实验为**无效结论**——那次实验尚未排除其它变量，需在当前已净化的前提下重做 |
| C2 | 纹理采样：`sprite_batch.frag` 采样 `u_Texture`，真实 batch 走 `OpenGLTexture` + `m_CurrentTexture->Bind(0)` | 若纹理不完整，采样返回 (0,0,0,1) → 应表现为**黑色**而非 clear color，故优先级较低 |
| C3 | `m_SpriteCount` 跨纹理分组从不清零（`End()`→`SubmitBatch()` 不重置，只有 `Flush()` 重置） | 行为差异，疑似冗余上传而非正确性问题 |
| C4 | 真实 batch 的顶点上传路径（`m_VertexBuffer` 尺寸/上传时机） | A3 已证明等价设置序列合法，故此项预期较低 |

清理状态：A2-rerun 临时 seam 与临时用例已完整删除；`assets/`、`OpenGLSpriteBatch.*` 未被本轮改动。
清理后复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）。

### 5.10 A5 结果（冻结）：**C1 排除**（四个管线状态都不是原因），但实验有一个必须说明的弱点

control = 四个状态全部显式关闭；每个变体只"释放"一个（即不去设置它、保留继承值），
其余三个仍显式关闭；每个变体都记录 4 个状态的实测值。

```text
[A5] rc=0
[A5] control(all off)   magenta=6552 state[scissor=0 cull=0 depth=0 blend=0]
[A5] release scissor    magenta=6552 state[scissor=0 cull=0 depth=0 blend=0]
[A5] release cull       magenta=6552 state[scissor=0 cull=0 depth=0 blend=0]
[A5] release depth      magenta=6552 state[scissor=0 cull=0 depth=0 blend=0]
[A5] release blend      magenta=6552 state[scissor=0 cull=0 depth=0 blend=0]
```

**结论：C1 排除。** 五个变体输出完全一致，且实测状态显示**这四个状态在生产上下文中本来就全是关闭的**
（与此前在真实 production render 之后测到的 `scissor=0 / cull=0 / depth=0 / blend=0` 一致）。
既然"释放"没有改变任何实际状态、也没有改变输出，**管线状态不解释缺失的片元**。

**必须同时说明的两个弱点（不掩盖）**：

1. **"释放"实际上是空操作**：四个状态在进入实验时就已经是关闭的，所以本实验**没有**真正测试
   "若它们被打开是否会吃掉片元"。它证明的是"生产上下文里它们没开、且不打开时 payload 能出片元"
   ⇒ 排除 C1 作为**当前**的差异来源；它**不等于**证明"打开任何一个都会导致 0 片元"。
2. **control 的绝对值与 A2R 不一致**：A5 control = 6,552，A2R = 91,656。原因是 A5 的 harness
   **没有在每个变体前清空 presentation target**，五次 draw 的品红在缓冲里累积/重叠，因此各变体的
   绝对计数不可与 A2R 横向比较。C1 的结论仍然成立，因为五个变体在**完全相同**的条件下得到**完全相同**
   的输出；但"harness 等价于 A2R"这一点**未被证明**。

**清理与复核**：A5 临时 seam 与临时用例已完整删除；`assets/`、`OpenGLSpriteBatch.*` 未被本轮改动。
`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）。

### 5.11 A6 结果（冻结）：**C2 排除**（采样通道不是 B1）+ **A2R 数字的自我修正**

按批准的 A6 设计执行：真实 `assets/shaders/sprite_batch.vert` / `sprite_batch.frag`（**从磁盘读真实
资源**，非转写）、真实场景纹理、按纹理分组 draw（镜像 `SceneRenderer` + `Begin/End` 语义）。
**每个变体独立清空 target → draw → 一次 readback**（无跨变体累积）。

```text
[A6] rc=0
     control (常量品红 frag,        nonClear) = 6552
     A6-1   (真实 frag + 真实纹理 + u_Texture=0) = 6552
     A6-2   (真实 frag + 真实纹理 + 不设 u_Texture) = 6552
     samplerLoc=0  afterDraw=0x0  afterRead=0x0
```

| 观测 | 判定 |
|---|---|
| `A6-1 = 6552` | 真实 shader + 真实纹理 + 显式 sampler → **能出片元**，采样路径本身可用 |
| `A6-2 = 6552` | 不设 `u_Texture` **同样出片元** |
| `afterDraw / afterRead = 0x0` | 真实 shader / 纹理 / 采样全程无 GL 错误 |
| 三者相等 | 与 A2R 结论一致：片元阶段可工作 |

**结论（按矩阵第 2 行）：C2 排除。** 纹理采样不是 B1；采样 uniform 的不对称**不是缺陷** ——
采样器 uniform 的默认值恰好是 0，而 batch 恰好把纹理 `Bind(0)`，二者一致。**C2 冻结，不再追。**

#### 5.11.1 必须记录的自我修正：payload 的真实片元数约 6,552，而非 91,656

A6 的 control = **6,552**（独立清空、单次 draw），与 A5 control 完全相同；而 A2R 报的是 91,656。
原因**不在 harness 缺陷**，而在 **A2R 自身没有在 control 三角形与 payload draw 之间清空**：

```text
86,400 (A1 control 三角形) + 5,256 (payload 未被三角形覆盖的部分) = 91,656
6,552 (payload 全量) − 5,256 (payload 覆盖到三角形的部分) = 1,296   → 与 6,552 自洽
```

即 A2R 的 91,656 = control 残留 + payload；**payload 自身约 6,552 像素**。
- ✅ 仍然成立的结论：A2R 的**定性结论完全不变** —— payload 确实能出片元，B1 不在 scene payload /
  索引式 / 顶点坐标 / 正交取景 / 属性布局 / EBO 区间 / VAO-VBO 序列。
- ❌ 不能再引用的数字：`91,656` 不得再作为 baseline、不得与其它变体横向比较。
- 📌 因此从现在起，**基线 = 每变体独立清空后的 nonClear 像素数（本轮 control = 6,552）**。

#### 5.11.2 清理与复核
A6 临时 seam（`RunDiagnosticSamplerIsolation`）与临时用例已**完整删除**（残留 0）；
`assets/shaders/*`、`OpenGLSpriteBatch.*`、生产 shader/纹理代码**均未改动**；C ABI 未变。
复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）；
`test_hrc` 15/15、`test_scripting` 27/27、exit 0（Debug）；`git diff --check` 0。

### 5.12 A7 结果（冻结）：**真实 `OpenGLSpriteBatch` 能出片元 ⇒ B1 回到我方 production render step**

按批准范围执行：直接实例化生产 `OpenGLSpriteBatch`，**不修改 `OpenGLSpriteBatch.*`**。
构造 / 全部 GL 使用 / 析构均在 runtime 线程且 context current（B2 硬约束）。
仅诊断侧观测，未在生产代码中加任何诊断。

```text
[A7] rc=0 groups=3 draws=6
[A7] variant=fitted-ortho nonClear=7296  spriteCounts=1,4,4  phases: preCtor/postCtor/postBegin/postEnd/postRead/postDtor 全部 0x0
[A7] variant=identity    nonClear=120000 spriteCounts=1,4,4 phases: 全部 0x0
```

| 观测 | 判定 |
|---|---|
| `nonClear = 7,296`（fitted）/ `120,000`（identity） | **生产 batch 本身能出片元**；0 vs 非 0 的判别器为"非 0" |
| `spriteCounts = 1,4,4`（合计 9） | 与场景 9 个 sprite 一致，`Begin/Draw` 收数正确 |
| `draws = 6`（每变体 3） | 3 个纹理分组各发一次 draw，与 `SceneRenderer` 分组语义一致 |
| 六个阶段 `glError` 全 `0x0` | 构造/Begin/End/readback/析构全程无 GL 错误 |
| 无构造/析构回归 | 不触发 B2 冻结条件 |

**结论（矩阵第 1 行）：B1 不在 `OpenGLSpriteBatch` 内部。** B1 回到**我方 production render step 的集成**：
调用顺序、buffer 尺寸/时机、状态切换，或**喂给 batch 的相机矩阵**。
计数 7,296 / 120,000 **不要求等于 6,552**（用户明确授权：只判 0 vs 非 0），差异属
矩阵/覆盖范围差异，仅作证据保留。

#### 5.12.1 顺带证伪的两个旧候选（读码结论，非新实验）

| 候选 | 原假设 | 实际 | 结论 |
|---|---|---|---|
| C3 | `m_SpriteCount` 跨纹理分组从不清零 | `Begin()` 内 `m_SpriteCount = 0`（`OpenGLSpriteBatch.cpp:140`） | ❌ **证伪**：计数每组都从 0 起；A7 实测 `1,4,4` 亦印证 |
| C4 | `m_VertexBuffer` 尺寸/上传时机 | `SetCapacity` **仅**由构造函数调用（`:44`）；`:39` 在 `m_VAO` 绑定期间绑 EBO，故 `m_VAO` 的 element binding 正确；随后 `:62` 把 VAO0 的 binding 复位为 0，无污染 | ❌ **证伪**（读码；A7 绘制成功亦反证） |

⇒ **C1、C2、C3、C4 全部排除。`OpenGLSpriteBatch` 洗清。** 唯一剩余嫌疑在
`Application::RenderProductionFrame`（我方 P1 新代码）与其相机矩阵。

#### 5.12.2 清理与复核
A7 临时 seam（`RunDiagnosticRealBatchObservation`）、临时用例、临时 include
（`OpenGL/Resources/OpenGLSpriteBatch.h`、`<fstream>`、`<iterator>`）、以及
`bridge/CMakeLists.txt` 中临时 `engine/src` include 路径，**全部已删除（残留 0）**。
`OpenGLSpriteBatch.*` 与 `assets/shaders/*` **零改动**；C ABI 未变。
复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）；
`git diff --check` 0。

#### 5.12.3 下一步（有界，读码优先，不改 `OpenGLSpriteBatch`）
1. **先读码** `Application::InitProductionRenderResources` / `RenderProductionFrame`：
   `m_ProductionCamera` 是否有效、`u_ViewProjection` 是否真的在 draw 前被设置、
   `SceneRenderer::Render` 的 `!m_Shader || !m_Camera` 早退是否命中、
   `SetRenderContext` 是否已创建 batch、`RenderableCount` 是否 > 0。
2. 若读码不能定论，再做一次**只读观测**（临时 seam，仅记录 renderable count /
   相机矩阵 / 早退命中与否），**不修复**。
3. 只有拿到上述证据后，才开一个**窄范围 production fix slice**，自带验收与回归证据。

### 5.13 `RenderProductionFrame` 只读源码检查（无 patch、无 seam）

按 §5.12.3 顺序逐项读码。四项**确定性候选全部排除**，代码路径**仍然歧义**。

#### 5.13.1 相机有效性与矩阵构造 —— **排除**

| 检查 | 结论 |
|---|---|
| 取景算法 | `Application.cpp:674-704`：由 `cmd.worldMatrix` 列长 + `m[12]/m[13]` 求 AABB，10% 边距，按 `targetWidth/targetHeight` 补齐宽高比 —— 与 A7 `fitted` **同一算法** |
| 相机构造 | `OrthographicCamera(l,r,b,t)` → `glm::ortho(l,r,b,t,-1,1)`，`view = identity`，`vp = proj * view`（`OrthographicCamera.cpp`） |
| **逐元素对比** | glm column-major 与 A7 手写 `fitted`：`[0][0]=2/(r-l)`、`[1][1]=2/(t-b)`、`[2][2]=-2/(f-n)=-1`、`[3][0]=-(r+l)/(r-l)`、`[3][1]=-(t+b)/(t-b)`、`[2][3]=-(f+n)/(f-n)=0`、`[3][3]=1` —— **完全一致**；而 A7 `fitted` 产出 **7,296** non-clear |
| 历史实测自洽 | §3.1 记录的生产相机 `(-10.45,10.45)×(-7.84,7.84)`，宽高比 `20.9/15.675 = 1.3333 = 800/600` ✓ |
| uniform 传递 | `OpenGLShader::SetMat4` → `glUniformMatrix4fv(loc, 1, GL_FALSE, data)`，**transpose = `GL_FALSE`**，与 A7 原始调用一致（**排除转置 bug**） |
| 非空 | `m_ProductionCamera.get()` 于 `:704` 传入，紧接 `:710` `Render()`；`m_Headless` 为 true（`EngineHost.cpp:529` 依赖之），`:605` 守卫不拦 |

#### 5.13.2 shader/camera 早退 —— **排除**
`m_Shader` 于 `:632` 设置，`m_Camera` 于 `:704` 设置，`Render()` 于 `:710` 调用。
`if (!m_Shader || !m_Camera) return;` **不可能命中**。

#### 5.13.3 `SetRenderContext()` 构造路径 —— 对象存在且类正确
`:631 SetRenderContext(context)` → `m_SpriteBatch = m_Factory.CreateSpriteBatch(context)`
（`SceneRenderer.cpp`）。即**与 A7 直接实例化并成功出片元的同一个 `OpenGLSpriteBatch` 类**，
且 `Render()` 是它唯一的喂入者（无其它 feed 路径）。

#### 5.13.4 renderable count —— 存在**测量缺口**（真实缺陷，但非本案根因）
`m_ProductionRenderableCount = commands.size()`（`:712`）取自 `:658-659` **独立** `probeQueue`，
在 `Render()` **之前**收集。因此它统计"收集到的命令"，**不反映 `Render()` 实际画了什么**，
无法观测 `SceneRenderer::Render` 中的 `if (!tex) continue;` 跳过。
另注：`Render(scene)` 使用**默认参数 `defaultTexture = nullptr`**（`SceneRenderer.h:52`），
故生产完全依赖 `BindEditSceneSpriteTextures()` 绑好纹理。
但该跳过**未发生**：§1.5/§3.1 记录 `textured=9`，且 A6/A7 独立观测到 3 个互不相同的非空纹理（`1,4,4`）。

#### 5.13.5 剩余唯一未读的结构性差异：对象/上下文生命周期

| | A7（能出片元） | 生产（0 片元） |
|---|---|---|
| batch 构造时机 | **在绘制帧内**，runtime 线程、context current | `InitProductionRenderResources` 内一次创建，`m_ProductionRenderReady` 锁存（`:602-604`、`:636`），**跨帧、跨 host 存活** |
| 生命周期 | 帧末析构 | 长驻 |

这与 B2 的特征签名同类：sprite-batch GL 对象若在**非绘制帧的 context** 下创建，会导致
**后续 host 的 GLFW 初始化失败**；若其 VAO/VBO/EBO 归属另一 context，绘制可**静默 no-op 且无 GL 错误** ——
与 B1 的"合法 draw、零 GL 错误、零片元"完全吻合。**这仍是假设，不是已证结论。**

**裁决：读码无法给出确定性解释 ⇒ 按 stop rule，下一步是**一个**只读观测 seam**
（暴露能区分剩余假设的最小事实集），**不做任何修复**。

### 5.14 只读 seam 观测结果（冻结）：**喂入/程序/矩阵全部正确；像素指标不可信（我自己的测量缺陷）**

seam 逐步复刻生产块序列（`BindEditSceneSpriteTextures` → FBO bind → clear → viewport →
`RenderProductionFrame`），在既有 runtime 帧、既有 context current 下**只读**采集。

```text
[SEAM] rc=0 renderOk=1 nonClear=480000 spriteCount=4 renderables=9 glError=0x502
[SEAM] GL VAO=0 VBO=1 EBO=0 program=3 linkStatus=1
[SEAM] matrix(colmaj): 0.0956938 0 0 0  0 0.127592 0 0  0 0 -1 0  0 0 0 1
```

#### 5.14.1 逐项判定（严格按矩阵，且不越权解读）

| # | 观测 | 判定 |
|---|---|---|
| 6 | `renderables = 9` | ✅ 收集正确 |
| 2 | `spriteCount = 4` | ✅ **喂入正确**（= 最后一纹理组的 sprite 数，见 §5.14.3）⇒ **排除**"count>0 但 sprite count 0"的喂入差异 |
| 5 | `program = 3`，`linkStatus = 1`(GL_TRUE) | ✅ **生产 program 有效** ⇒ **排除** H_b（坏 program） |
| 4 | `VAO = 0`、`EBO = 0`（`SubmitBatch` 末尾 `BindVertexArray(0)` 之后的**帧末**状态） | ⚠ 是 **post-draw** 状态，**不是 draw 时刻**；按用户约束**不由名字推断 context 归属**，不作结论 |
| 3 | 矩阵 `[0][0]=0.0956938`、`[1][1]=0.127592`、`[2][2]=-1`、`[3][0]=[3][1]=0`、`[3][3]=1` | ✅ **合法且良构**；`2/20.9=0.09569378`、`2/15.675=0.1275917` ⇒ 与 §3.1 相机 `(-10.45,10.45)×(-7.84,7.84)` 逐位吻合；`[3][*]=0` 因场景中心在原点（合法）。**与 A7 `fitted` 矩阵逐元素完全一致** |
| 1 | `nonClear = 480000`（= 全部 480,000 像素） | ❌ **不可信，见 §5.14.2** |
| — | `glError = 0x502` = **`GL_INVALID_OPERATION`** | ⚠ **B1 从未记录到该错误**（B1 记 `0x0`）；本次出现，**需归因后才能解读** |

#### 5.14.2 我自己的测量缺陷：`nonClear=480000` 不能作为"已渲染"证据

**不能**据此宣布"像素出现 ⇒ B1 证据过期 ⇒ P1 解封"。理由是自洽的反证：

1. 生产矩阵与 A7 `fitted` **逐元素完全相同**（§5.14.1 #3），而 A7 `fitted` 产出 **7,296** non-clear。
2. 若生产真的用同一矩阵渲染，期望值应 ≈7,296，**不可能是 480,000**。
3. ⇒ `480000` 只能是**我的读回失败**导致 `px` 保持零填充，而 `(0,0,0)` 与 clear `(13,13,18)` 不同 ⇒ 被计成"非 clear"。
4. 同一次调用尾部 `glError = 0x502` 正是该读回失败的直接症状。

**归因**：`ReadPixels` 抛 `INVALID_OPERATION` 的常见原因是 **framebuffer 不完整**或
**读缓冲名无效**。我的 seam 用**裸 GL**（`glReadBuffer` + `glReadPixels`），
而仓库既有的 `ReadPresentationPixels` seam 走引擎自己的 FBO 读回帮助函数 ——
**差异在读回路径本身**。即 `0x502` 极可能来自 seam 自加的裸 GL 读回，**而非生产渲染**。

⇒ 因此：本次 seam **没有**给出"像素是否出现"的答案；它给出的是
**四个确定性正确项**（喂入 / program / 矩阵 / renderable 计数）+ **一个待归因的 GL 错误**。

#### 5.14.3 `9 命令 / 3 纹理 ⇒ 4 次 batch` 分歧：**已识别，两份收据都保留**

- §3.1 记录 `batchSprites=4`，并解释为"4 次 batch"。
- A7 观测 `groups=3`、`draws=3`（每变体）。

**结论**：`4` 与 `3` 度量的**不是同一个东西**，且 §3.1 的解释有误：
`4` 是 `ISpriteBatch::GetSpriteCount()` = `m_SpriteCount` = **最后一个纹理组的 sprite 数**
（A7 实测最后一组正是 `4`）；它**从不是** batch 计数器。
**batch 数是 A7 的 `draws=3`**。两份收据均保留，不做事后改写。

#### 5.14.4 清理与复核
seam（`ObserveProductionRenderFrame`）、两个 `Application` 只读访问器、临时用例
**全部已删除（残留 0）**；生产代码回到 seam 前状态，`kEnableProductionSceneRender` 始终为 `false`
（seam 复刻序列，**未**改动每帧门控）。
复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）；
`OpenGLSpriteBatch.*` / `assets/` 零改动；C ABI 未变；`git diff --check` 0。

#### 5.14.5 裁决：仍有多重活假设 ⇒ **停下**，不发明新的大实验

按 stop rule，**不**再加第二个 seam 去证明 context 身份。下一 slice 应**由上述已捕获状态设计**：

| 剩余活假设 | 已排除的部分 | 仍需的最小事实 |
|---|---|---|
| H_pixel：生产确实出片元，B1 证据过期 | — | **用引擎自己的 FBO 读回**（非裸 GL）重测 presentation FBO 像素；避开本次 `0x502` 归因问题 |
| H_glerr：`0x502` 来自生产渲染而非 seam 读回 | seam 读回是首要嫌疑 | 把 `GetError` **分段**取样（Render 前 / Render 后 / 读回后）即可归因 |
| H_ctx：batch GL 对象跨 context | 喂入、program、矩阵均已排除 | 仍需，但**必须先**排掉上面两项，否则无法归因 |

**建议下一切片**（一次、单变量、只读）：**分段取 `glError` + 改用引擎 FBO 读回测像素**。
这两件事在同一次调用里即可闭合 H_glerr 与 H_pixel，且不触碰 `OpenGLSpriteBatch`、不新增 ABI。

### 5.15 分段 glError + 既有读回路径（权威像素测量）结果（冻结）：**B1 确认成立，不是过期证据**

```text
[SEG] rc=0 renderOk=1 total=480000 nonClear=0 sentinelUnchanged=0
[SEG] glError preRender=0x0 postRender=0x0 postReadback=0x0 | renderables=9 spriteCount=4
```

| 观测 | 判定 |
|---|---|
| `sentinelUnchanged = 0` | ✅ **读回成功**（缓冲预填 `0xAB`，全部被真实数据覆盖）⇒ 上一次 seam 的 `0x502` 与 `480000` **100% 是该 seam 自身裸 GL 读回污染**，已归因结案 |
| `nonClear = 0` / `total = 480000` | ✅ **整帧全部是 clear color** ⇒ **B1 确认：生产帧确实没有片元。B1 证据不过期。** 我上一轮"B1 可能过期"的怀疑被**否证** |
| `preRender = postRender = postReadback = 0x0` | ✅ **渲染路径与读回路径都不产生任何 GL 错误** ⇒ H_glerr（`0x502` 来自生产）**排除** |
| `renderables = 9`、`spriteCount = 4` | ✅ 喂入正确、收集正确（`4` = 最后一纹理组 sprite 数，非 batch 数；**batch 数 = A7 的 3**） |

**对照 §5.14 的两处自我修正**：
1. `nonClear=480000` = 读回失败导致的零填充假象，**已推翻**（本次 `sentinelUnchanged=0` 证明读回正常）。
2. "B1 证据可能过期" = **错误推测**，**已推翻**（本次权威测量 `nonClear=0`）。

#### 5.15.1 当前已排除 / 已确认的完整清单

| 类别 | 状态 |
|---|---|
| raster 基础设施、payload/索引/顶点数据、VAO-VBO-EBO 序列、诊断顺序 | ✅ 已排除 |
| C1 管线状态（scissor/cull/depth/blend） | ✅ 已排除 |
| C2 纹理采样 | ✅ 已排除 |
| C3 `m_SpriteCount`、C4 上传路径 | ✅ 已排除（读码 + A7） |
| `OpenGLSpriteBatch` 本身 | ✅ 已排除（A7 直接实例化出片元） |
| 相机/矩阵（含 `GL_FALSE` 转置） | ✅ 已排除（与 A7 `fitted` 逐元素一致） |
| 早退 `!m_Shader \|\| !m_Camera` | ✅ 未命中 |
| 喂入（`cmd.texture` 已绑、`textured=9`） | ✅ 正确 |
| GL 错误（生产渲染 / 读回） | ✅ 全 `0x0` |
| **B1 现象本身** | ❌ **确认：整帧 clear，零片元** |

#### 5.15.2 由已捕获状态得出的下一个假设（**优先于 H_ctx**）

用户口径：只有"渲染与读回都干净、而输出仍为空"才把 **context/资源生命周期**作为下一个隔离假设。
**该前提现在成立。** 但在花代价做 context 身份取证之前，有一个**更廉价且直接命中盲区**的候选：

> **`glColorMask`（颜色写掩码）—— A5 从未测过它。**

- A5 只枚举了 scissor/cull/depth/blend **四个**状态；**color mask 不在其中**。
- 颜色写掩码为全 FALSE 时，绘制**照常提交、glError 全 0，但一个片元都不会写入** ——
  与 B1 的"合法 draw、零错误、零片元"**完全吻合**。
- 生产帧在同一帧内先做 64×64 探针 FBO clear（G2P-01）与既有 pick 准备
  （`OpenGLFramebuffer::BindForPickRendering()` 存在，pick 常与写掩码/单通道附件联动），
  再做 production render ⇒ **前序帧工作遗留脏状态**是 A7（新建 batch、无前序工作）不会复现的唯一差异类别。
- 同类"能抑制光栅化且不报错"的次级候选：**viewport 尺寸**、`GL_POLYGON_MODE`、stencil test。

⇒ **建议下一切片**：在**同一帧**内分段记录 `glGetBooleanv(GL_COLOR_WRITEMASK)`、
`glGetIntegerv(GL_VIEWPORT)`、`glGetIntegerv(GL_POLYGON_MODE)`、`glIsEnabled(GL_STENCIL_TEST)`
（pre-Render / post-Render 各一次），**只读、单变量**。若 `colorMask` 为全 FALSE 即**当场定位**。
仍**不**做 context 身份取证，也**不**修复。

#### 5.15.3 清理与复核
本轮 seam（`ObserveProductionRenderSegmented`）、`Application` 只读访问器、临时用例
**全部已删除（残留 0）**；`kEnableProductionSceneRender` 全程保持 `false`。
复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）；
`test_hrc` 15/15；`OpenGLSpriteBatch.*` / `assets/` 零改动；C ABI 未变；`git diff --check` 0。

### 5.16 继承态光栅化状态切片结果（冻结）：**四项全部正常 ⇒ 该状态族排除**

纯观测，未修改任何状态（未设 colorMask、未重置 viewport、未关 stencil）。

```text
[RASTER] rc=0 renderOk=1 renderables=9 spriteCount=4
[RASTER] PRE  colorMask={T,T,T,T} viewport=[0,0,800,600] polygon=[6914,-1] stencil=0 glErr=0x0
[RASTER] POST colorMask={T,T,T,T} viewport=[0,0,800,600] polygon=[6914,-1] stencil=0 glErr=0x0
[RASTER] pixels nonClear=0 sentinel=0 total=480000 readbackErr=0x0
```

| 观测 | 判定 | 结论 |
|---|---|---|
| `colorMask = {T,T,T,T}` | ❌ **不是 color mask** | 我的主假设被证伪 |
| `viewport = [0,0,800,600]` | ✅ 与 800×600 目标一致 | 排除 |
| `polygon = [6914, -1]` | `6914 = 0x1B02 = GL_FILL`（`GL_POLYGON_MODE` 核心配置只返回 1 个值，第二槽是我的 `-1` 初值） | 排除 |
| `stencil = 0` | ✅ 关闭 | 排除 |
| **PRE 与 POST 四项完全相同** | 生产渲染路径**自身不改动**这四项 | 一并冻结（说明它们不是被生产代码"修好"的） |
| `nonClear=0 / 480000`，`sentinel=0`，三段 `0x0` | B1 仍成立，读回可信 | — |

**裁决：按矩阵最后一行 —— 四项均正常，该状态族消除。** 不扩展成通用 GL 状态审计。

#### 5.16.1 下一候选（**从剩余状态差选定，不是泛化审计**）

**A5 的"四项全 0"是在 A5 自己那次探针里、且在它自己 `Disable` 之后测的，
位置在 A5 诊断帧内，不等于生产渲染真正继承到的状态。**
因此 **scissor / cull / depth / blend 从未在"生产帧的真实 pre-Render 点"被测量过**。

其中 **scissor** 风险最高：scissor test 开启且 box 退化（例如遗留自 64×64 探针 FBO 工作、
或尺寸为 0）时，绘制照常提交、`glError` 全 0、但**零片元** —— 与 B1 完全吻合。
旁证：§3.2 已实测该渲染路径会**泄漏 GL 状态**并破坏下一帧 64×64 探针的 clear-color 校验，
说明这条路径与状态确有交互。

**建议下一切片（纯观测、单变量、同形状）**：在 pre-Render / post-Render 各记录
`GL_SCISSOR_TEST` + `GL_SCISSOR_BOX(4)`、`GL_CULL_FACE`、`GL_DEPTH_TEST`、`GL_BLEND`，
并补 `GL_SAMPLE_COVERAGE` / `GL_SAMPLE_ALPHA_TO_COVERAGE`（两者同样能"零错误零片元"）。
仍**不做** context 身份取证、**不**修复。

#### 5.16.2 清理与复核
本轮 seam（`ObserveProductionRasterState`）、`Application` 只读访问器、临时用例
**全部已删除（残留 0）**；`kEnableProductionSceneRender` 全程 `false`；
未修改任何 GL 状态（严格纯观测）。
复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）；
`OpenGLSpriteBatch.*` / `assets/` 零改动；C ABI 未变；`git diff --check` 0。

### 5.17 真正 pre-Render 点的继承态切片（冻结）：**scissor 关闭 ⇒ 状态族再次消除**

纯观测，未修改任何状态。

```text
[SCISSOR] rc=0 renderOk=1 renderables=9 spriteCount=4
[SCISSOR] PRE  scissor=0 box=[0,0,120,64] cull=0 depth=0 blend=0 sampleCov=0 alphaToCov=0
[SCISSOR] POST scissor=0 box=[0,0,120,64] cull=0 depth=0 blend=0 sampleCov=0 alphaToCov=0
[SCISSOR] pixels nonClear=0 sentinel=0 total=480000 | glErr pre=0x0 post=0x0 readback=0x0
```

| 观测 | 判定 |
|---|---|
| `scissor = 0` | ❌ **不是 scissor**：假设被证伪 |
| `cull = 0`、`depth = 0`、`blend = 0` | ✅ 正常（**这是生产入口的真实值**，A5 未测到过） |
| `sampleCov = 0`、`alphaToCov = 0` | ✅ 正常 |
| PRE == POST（六项全同） | 生产渲染路径**不改动**这些状态 |
| `nonClear=0 / 480000`，`sentinel=0`，三段 `0x0` | B1 仍成立 |

**A5 的 C1 结论据此精确定位**：A5 只证明了"在 A5 自己的探针里四项全关、且该条件下 payload 能出片元"，
**并未证明生产入口状态**。本切片补上后者：**生产入口四项也确实全关**。两者结论一致，但 A5 的表述过强，已在此更正。

**⚠ 潜在隐患（非根因，记录备查）**：`GL_SCISSOR_BOX = [0,0,120,64]` 是一个**遗留值**。
scissor 当前关闭故无影响，但**一旦该状态被开启，整帧会被裁剪到 120×64 = 7,680 像素**。
这是"脏状态"确实在帧间流动的又一处实证（与 §3.2 的状态泄漏证据同向）。

#### 5.17.1 下一候选：**最窄的剩余差 = 生产 program 里 `u_ViewProjection` 是否真的存在并被写入**

A7 与生产之间，所有已测项现已全部一致（管线状态族、program 有效且已链接、矩阵逐元素相同、
真实 batch 可渲染、喂入正确、真实纹理可采样）。剩下**唯一从未被测量**的机械点：

> `OpenGLShader::SetMat4` 的实现是
> `if (location != -1) glUniformMatrix4fv(location, 1, GL_FALSE, data);`
> ——**location 为 -1 时静默跳过，不报任何错**。
> 若生产 program 里 `u_ViewProjection` 的 location 是 -1（被优化掉 / 名称不匹配 /
> 绑到了别的 program），则该矩阵**从未上传**，`u_ViewProjection` 保持**全 0 默认值** ⇒
> `gl_Position = 0 * vec4(pos,1) = vec4(0,0,0,0)`，`w = 0` ⇒ 整个图元被裁掉 ⇒
> **零片元、glError 全 0**。与 B1 完全吻合。
>
> 我此前只验证过 **program 已链接**（`linkStatus=1`、`program=3`），
> **从未验证过 `u_ViewProjection` 的 location 非 -1**。
> A6 打印过 `u_Texture` 的 location（=0），但 A7/A6 都**没有打印过 `u_ViewProjection` 的 location**。

**建议下一切片（纯观测、单变量、非泛化审计）**：在生产帧内只读记录
`glGetUniformLocation(currentProgram, "u_ViewProjection")`、
`glGetUniformLocation(currentProgram, "u_Texture")`，
以及 `glGetUniformfv(prog, loc, 16 个 float)` **回读 uniform 当前值**，
与 A7 `fitted` 矩阵逐元素比对。若 location == -1 或回读值全 0 ⇒ **当场定位 B1**。

#### 5.17.2 清理与复核
本轮 seam（`ObserveProductionScissorState`）、`Application` 只读访问器、临时用例
**全部已删除（残留 0）**；`kEnableProductionSceneRender` 全程 `false`；未修改任何 GL 状态。
复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）；
`test_hrc` 15/15；`OpenGLSpriteBatch.*` / `assets/` 零改动；C ABI 未变；`git diff --check` 0。

### 5.18 uniform 契约切片（冻结）：**`u_ViewProjection` 假设消除；生产 program 与 A7 完全一致**

采集点在 **`Render()` 之后**（程序仍 current，`SubmitBatch` 不解绑 program），
因此回答的是"生产路径是否把矩阵解析并上传到**它真正绘制所用的那个 program**"。

```text
[UNIFORM] rc=0 program=3 vpLoc=1 texLoc=0 linkStatus=1
[UNIFORM] renderOk=1 renderables=9 spriteCount=4 nonClear=0 sentinel=0 total=480000
[UNIFORM] glErr pre=0x0 post=0x0 readback=0x0
[UNIFORM] u_ViewProjection[16]（列主序，回读值）
[ 0.0956938 | 0 | 0 | 0 ]
[ 0 | 0.127592 | 0 | 0 ]
[ 0 | 0 | -1 | 0 ]
[ 0 | 0 | 0 | 1 ]
[UNIFORM] compare vs A7 fitted: nonZeroEntries=4 maxAbsDiff=2.83122e-07
```

| 观测 | 判定 |
|---|---|
| `vpLoc = 1`（**不是 -1**） | ❌ **"location 为 -1 导致静默跳过"假设被证伪** |
| `texLoc = 0`、`linkStatus = 1` | ✅ 均有效 |
| 回读矩阵 vs A7 `fitted` | **`maxAbsDiff = 2.83e-07`**（float 舍入量级）⇒ **上传成功且逐元素一致** |
| `nonClear=0 / 480000`、`sentinel=0`、三段 `0x0` | B1 仍成立 |

**⇒ uniform 假设消除。** 这是本轮最硬的一条：矩阵**确实**被上传了，而且和 A7 出片元时用的矩阵相同。

#### 5.18.1 读码发现：生产与 A7 **并不走同一条构造路径**（新的头号嫌疑）

A7 是**直接栈上构造** `OpenGLSpriteBatch`；生产走工厂：

```cpp
// OpenGLGraphicsFactory.cpp:435
std::shared_ptr<ISpriteBatch> OpenGLGraphicsFactory::CreateSpriteBatch(IRenderContext& ctx) {
    if (m_Allocator) {
        StackAllocatorAdaptor<OpenGLSpriteBatch> adaptor(m_Allocator);
        return std::allocate_shared<OpenGLSpriteBatch>(adaptor, m_GL, ctx);   // ← 生产走这条
    }
    return std::make_shared<OpenGLSpriteBatch>(m_GL, ctx);                     // ← A7 等价于这条
}
```

而 `m_Allocator` **在 EngineHost 下确实非空**：`InitializeHeadless()` 里
`m_Factory.SetAllocator(&m_SubsystemAllocator)`（`Application.cpp:126`），
池容量 `StackAllocator m_SubsystemAllocator{256 * 1024}`（`Application.h:231`）。

⇒ **生产 batch 的对象内存来自一个 256KB 的 bump 池，而非普通堆。**
这正是此前被推迟的"资源生命周期"族里**唯一还没被验证的具体机制**，
而且它是 A7 与生产之间**仅存的构造差异**。

**必须同时说明的反证（不夸大）**：全仓 `m_SubsystemAllocator` **没有任何 `Reset()` 调用**
（只有 `:126`/`:127`、`:735`/`:736` 两处 `SetAllocator`/全局指针赋值），
所以我**不能**声称"帧末回卷导致对象内存被覆盖"。本帧内创建+绘制时内存应当是完好的，
这意味着该机制**尚不能单独解释首帧零片元**。它更可能解释 **B2**（跨帧/跨 host 状态损坏）。

#### 5.18.2 下一切片建议（单变量、直接区分"工厂/池构造"与"直接构造"）

在同一帧内做**对照绘制**，其它一切完全相同（同一 presentation FBO、同一 clear、同一 viewport、
同一真实 program、同一已上传的 `u_ViewProjection`、同一 payload、同一纹理分组）：

| 变体 | batch 来源 |
|---|---|
| A（对照） | 直接栈上构造 `OpenGLSpriteBatch`（= A7 路径，已知出片元） |
| B | `m_Factory.CreateSpriteBatch(*m_RenderContext)`（= 生产路径，走 256KB 池） |

每变体独立清空 + 一次读回（`0xAB` 哨兵）。判定只看 0 vs 非 0：
**A 出片元且 B 为 0 ⇒ B1 定位到工厂/池构造路径**（确定性结论，无需 context 取证）；
两者都出片元 ⇒ 该路径也排除，剩余差只可能是"每帧复用 vs 每帧新建"。

仍**不修复**、**不改** `OpenGLSpriteBatch.*`、**不做** context 身份取证。

#### 5.18.3 清理与复核
本轮 seam（`ObserveProductionUniforms`）、`Application` 只读访问器、临时用例
**全部已删除（残留 0）**；`kEnableProductionSceneRender` 全程 `false`；
未改 shader、未改 `SetMat4`、未修改任何 GL 状态。
复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）；
`test_hrc` 15/15、`test_scripting` 27/27；`OpenGLSpriteBatch.*` / `assets/` 零改动；
C ABI 未变；`git diff --check` 0。

### 5.19 构造路径 A/B 对照（冻结）：**工厂/池构造路径排除**

唯一变量 = batch 构造路径；其余输入（runtime 线程/context、presentation FBO、clear、viewport、
真实 program、`u_ViewProjection`、纹理绑定、9-sprite payload、分组顺序、读回路径 + `0xAB` 哨兵）
完全相同；每变体独立清空 + 一次读回；两个变体都在 context current 下创建 → 绘制 → 析构/释放。

```text
[AB] rc=0 groups=3 total=480000
[AB] A direct-stack   nonClear=7296 spriteCount=4 glErr=0x0 addr=0x1008843f220
[AB] B factory/pool   nonClear=7296 spriteCount=4 glErr=0x0 addr=0x229096819c8
[AB] preErr=0x0 distinctObjects=yes
```

| 变体 | 构造路径 | 结果 |
|---|---|---|
| A | 直接栈上构造（= A7 路径） | **7,296**（与 A7 `fitted` 基线逐字一致） |
| B | `m_Factory.CreateSpriteBatch()` → `StackAllocatorAdaptor` + `allocate_shared`（256KB 池） | **7,296** |

**⇒ 工厂/分配器构造路径排除。** 两者像素数、sprite count、GL 错误完全相同，且
`distinctObjects=yes` 确认是两个不同对象（不是同一对象被复用）。
按用户矩阵第 2 行：剩余生产差异 = **长生命周期/复用 batch vs 每帧新建**。

#### 5.19.1 但"每帧新建 vs 复用"也解释不了首帧 —— 需要更正一个推论

`kEnableProductionSceneRender` 全程 `false` ⇒ `PumpOneFrame` 从不调用 `RenderProductionFrame`
⇒ 首次调用发生在 seam 内 ⇒ `InitProductionRenderResources`（`Application.cpp:645` 惰性初始化）
在**同一帧内**创建 shader/batch/camera 后立即绘制。
**因此首帧的 batch 也是"当帧新建"的** ⇒ "复用 vs 新建"同样**不能**解释首帧零片元。
这条推论在此明确撤回，避免下一轮误判。

#### 5.19.2 收窄后的状态：已排除 vs 仍成立

| 已排除（实测） | 仍成立 |
|---|---|
| raster / payload / 索引 / 顶点数据 / 相机矩阵（与 A7 逐元素一致）/ 早退 / uniform 契约（`vpLoc=1`，回读 `maxAbsDiff=2.8e-07`）/ colorMask / viewport / polygonMode / stencil / scissor / cull / depth / blend / sampleCoverage / alphaToCoverage / GL 错误 / 工厂与分配器构造路径 / `OpenGLSpriteBatch` 本身 | **生产 draw 确实发生**（`spriteCount=4` ⇒ 末组 4 张已 `Begin/Draw/End`），**用正确 program + 正确矩阵 + 正确纹理**，但 presentation FBO 仍是 480,000/480,000 clear |

#### 5.19.3 下一切片建议（只读、最窄、两个"静默归零"机制）

`glDrawElements` 正常提交、`glError` 全 0、状态全部正常，却零片元 —— 只剩两类经典机制：

| 机制 | 为何从未测过 | 判别事实 |
|---|---|---|
| **① draw 进了别的 target** | 之前所有切片都只读 presentation FBO 的像素，**从未在 draw 之后读 `GL_DRAW_FRAMEBUFFER_BINDING`** | 若 draw 时的 FBO 不是 presentation FBO ⇒ 像素落在别处（例如 64×64 探针 FBO） |
| **② `GL_RASTERIZER_DISCARD` 开启** | 它是**全局**开关，开启时一切图元被丢弃：**零片元 + 零 GL 错误**。不在 A5、也不在 §5.16/§5.17 两轮状态清单里；全仓无任何代码设置/复位它 | `glIsEnabled(GL_RASTERIZER_DISCARD)` = 1 ⇒ **当场定位** |

**建议**：在 post-Render 只读记录
`GL_DRAW_FRAMEBUFFER_BINDING`、`GL_READ_FRAMEBUFFER_BINDING`、`glIsEnabled(GL_RASTERIZER_DISCARD)`、
`glIsEnabled(GL_SAMPLE_SHADING)` + `GL_MIN_SAMPLE_SHADING_VALUE`、`GL_DEPTH_WRITEMASK`，
pre/post 各一次；像素仍走既有读回路径 + `0xAB` 哨兵。
仍**不修复**、**不改** `OpenGLSpriteBatch.*`、**不做** context 身份取证。

#### 5.19.4 清理与复核
本轮 seam（`ObserveBatchConstructionPaths`）、临时 include（`OpenGLSpriteBatch.h`、`<fstream>`、`<iterator>`）、
`bridge/CMakeLists.txt` 临时 `engine/src` include、临时用例**全部已删除（残留 0）**。
复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）；
`test_hrc` 15/15、`test_scripting` 27/27；`kEnableProductionSceneRender` 仍为 `false`；
`OpenGLSpriteBatch.*` / `assets/` 零改动；C ABI 未变；`git diff --check` 0。
（过程中一次 include 替换因模式不匹配未生效导致编译报错，已修正并从干净构建复核。）

### 5.20 draw target / rasterizer discard（冻结）：**B1 根因确定 —— `Clear()` 内部 Unbind，渲染画到了默认帧缓冲**

```text
[TARGET] rc=0 presentationFboName=0 renderOk=1 renderables=9 spriteCount=4
[TARGET] PRE  drawFbo=0 readFbo=0 rasterDiscard=0 sampleShading=0 minSampleShading(x1000)=0 depthMask=1
[TARGET] POST drawFbo=0 readFbo=0 rasterDiscard=0 sampleShading=0 minSampleShading(x1000)=0 depthMask=1
[TARGET] pixels nonClear=0 sentinel=0 total=480000 | glErr pre=0x0 post=0x0 readback=0x0
```

**`drawFbo = 0` 是默认帧缓冲，不是 FBO。** 且这是在
`m_PresentationFbo->Bind()` **之后立刻**读到的（`presentationFboName=0`）。
`RASTERIZER_DISCARD=0`、`SAMPLE_SHADING=0`、`minSampleShading=0`、`DEPTH_WRITEMASK=1` 全部正常 ⇒
按矩阵第 1 行，**B1 定位到 draw-target 绑定**。

#### 5.20.1 根因（读码确定性解释，非推测）

`OpenGLFramebuffer::Clear()` 的实现是 **bind → clear → unbind**：

```cpp
// engine/src/OpenGL/OpenGLFramebuffer.cpp
void OpenGLFramebuffer::Clear(float r, float g, float b, float a) {
    if (!IsOwnerThread() || !glfwGetCurrentContext() || !m_FBO) return;
    Bind();                       // 绑定 m_FBO
    m_GL.ClearColor(r, g, b, a);
    m_GL.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Unbind();                     // ← 绑定回 0（默认帧缓冲）
}
```

而 P1 生产块的编排顺序是（`EngineHost.cpp:1221-1229`，**我自己的 P1 代码**）：

```cpp
m_PresentationFbo->Bind();                                  // 绑定 presentation FBO
m_PresentationFbo->Clear(0.05f, 0.05f, 0.07f, 1.0f);       // ← 内部 Bind+Unbind，**结束时绑定 0**
gl.Viewport(0, 0, 800, 600);
m_Application->RenderProductionFrame(...);                  // ← 此刻 draw target 已是**默认帧缓冲**
m_PresentationFbo->Unbind();
```

⇒ **`Clear()` 在返回时把 presentation FBO 解绑了**，随后的 production render 画到**默认帧缓冲**，
presentation FBO 只保留了 clear 色 ⇒ 读回必然 480,000/480,000 全 clear、`glError` 全 0。
读回路径本身是对的（它显式 `Bind()` 后才 `ReadPixels`），所以**读到的确实是 presentation FBO**，
而**写入确实没进它** —— 这正是"draw 与 read target 不一致"的表现。

**这一条同时解释了为什么此前所有排查都干净**：batch、payload、矩阵、uniform、program、
管线状态、工厂路径、GL 错误**全都是对的**；错的只是"渲染写进了哪个 target"。

⚠ 同时注意 `Bind()`/`Clear()` 的**静默提前返回**（`!IsOwnerThread() || !glfwGetCurrentContext()`）
本身也是一个独立的脆弱点，但**本例中未触发**（clear 确实生效了，故 `m_FBO` 非 0 且属主线程正确）。
记为隐患，非本案根因。

#### 5.20.2 修法方向（**尚未实施**，等待你批准 fix slice）

最小改动：**先 `Clear()`、再 `Bind()`**（或改用裸 GL clear 保持绑定），即让
"清理 → 绑定 → viewport → 渲染 → 解绑"的 target 始终一致。
不涉及 `OpenGLSpriteBatch`、不涉及 C ABI、不涉及 P2。

#### 5.20.3 清理与复核
本轮 seam（`ObserveProductionDrawTarget`）、`Application` 只读访问器、临时用例
**全部已删除（残留 0）**；`kEnableProductionSceneRender` 仍 `false`；未修改任何 GL 状态。
复核：`test_bridge` **41/41 PASSED + 1 DISABLED**、exit 0（**Debug + Release**）；
`OpenGLSpriteBatch.*` / `assets/` 零改动；C ABI 未变；`git diff --check` 0。

### 5.21 P1.1 修复切片（实施）：**B1 修复已验证；B2 仍是唯一阻塞 ⇒ P1.1 未关闭**

#### 5.21.1 修复内容（最小顺序更正 + 可执行守卫）

`EngineHost.cpp` production 渲染块：

```cpp
m_PresentationFbo->Clear(0.05f, 0.05f, 0.07f, 1.0f);   // ← 顺序更正：先 clear
m_PresentationFbo->Bind();                              // ← 再 bind（clear 内部会 unbind）
gl.Viewport(0, 0, 800, 600);
// 回归断言：渲染边界上 draw target 必须是 presentation FBO（只读检查）
GLint currentDrawFbo = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &currentDrawFbo);
if (currentDrawFbo != (GLint)m_PresentationFbo->GetFramebufferID() || expected == 0) {
    RequestEmergencyStop(); ... FillRejectedResult(..., "production render draw target is not the presentation FBO"); return -1;
}
m_Application->RenderProductionFrame(...);
m_PresentationFbo->Unbind();
```

`GetFramebufferID()` 是 `OpenGLFramebuffer` **既有**访问器，未新增任何访问器。
未改动：`OpenGLSpriteBatch`、`SceneRenderer`、shader/纹理、C ABI、P2、HRC-2 pump/ownership、
legacy `ViewportPanel`/`EngineEditor`。

#### 5.21.2 验收逐条核对

| # | 验收项 | 结果 | 证据 |
|---|---|---|---|
| 1 | presentation FBO 出现真实场景内容 | ✅ | `nonClear=7296`（= A7 fitted 基线逐字一致）、`renderables=9`、`target=800x600` |
| 2 | 引擎读回看到这些像素 | ✅ | `readbackBytes=1920000`（=480000×4）、`sentinel=0` |
| 3 | draw-FBO 断言证明渲染确实写入 presentation FBO | ✅ | 连续 3 个 pump cycle 全部返回 0；断言一旦不成立会 `return -1` 并使测试失败 |
| 4 | 无 GL 错误 | ✅ | 全部切片 `0x0`；断言通过说明 `GetFramebufferID()` 与实际绑定一致 |
| 5 | `RuntimeSeamProofThreeCycles` 保持 GREEN、无新 GL 状态泄漏 | ❌ **未达成** | 见 §5.21.3 |
| 6 | `test_bridge` 回到 41/41 + 1 DISABLED（Debug+Release） | ✅（门控关闭态） | Debug/Release 均 41/41 + 1 DISABLED、exit 0 |
| 7 | `test_hrc` 15/15、`test_scripting` 27/27 | ✅ | 均 GREEN、exit 0 |
| 8 | C ABI 未变 | ✅ | 本切片未新增/改动任何导出或结构体；`ENGINE_HOST_ABI_VERSION` 仍为 `((1<<16)\|1)` = v1.1；P1 的 presentation 常量是编译期常量、非 ABI increment |
| 9 | `git diff --check` 干净、残留进程 0 | ✅ | exit 0；残留 0 |
| 10 | 仅在 native 测试清单变化时更新 I2 | ✅ 不适用 | 验收证据用**既有** seam（`ReadPresentationPixels` / `LastProductionRenderableCount`）采集，临时用例已删除 ⇒ 清单未变，I2 仍 240 |

#### 5.21.3 门控打开时的实测：B2 确认，且**与 B1 相互独立**

临时把 `kEnableProductionSceneRender` 置为 `true` 后跑全量 `test_bridge`：

```text
[  FAILED  ] EngineHostAbiTest.RuntimeSeamProofThreeCycles
[  FAILED  ] EngineHostAbiTest.TEMP_P11Acceptance
2 FAILED TESTS   (exit 1)
...
[error] [OpenGLGraphicsFactory] failed to initialize GLFW   ← 后续 host 的 GLFW 初始化失败
```

- `RuntimeSeamProofThreeCycles` **单独运行 PASS**（1,995 ms），**在全量套件中失败**
  ⇒ **顺序相关的跨 host 污染**，与 §3.2 记录的 B2 现象一致。
- 单独运行验收用例时 `nonClear=7296` 成功；放进全量套件则失败 ⇒ 同一个 B2。

**结论：B1 已修好并有正面证据；B2（每帧真的渲染 ⇒ 后续 host GLFW 初始化失败 / 状态泄漏）
是 P1.1 关闭前的唯一剩余阻塞。** 因此 `kEnableProductionSceneRender` **必须保持 `false`**，
P1.1 **不得**宣布关闭。

#### 5.21.4 冻结状态
- 修复（顺序更正 + 断言）**保留在代码中**；门控**恢复为 `false`**。
- 临时验收用例已删除（残留 `TEMP_` = 0）。
- 复核：`test_bridge` 41/41 + 1 DISABLED（Debug + Release）、`test_hrc` 15/15、
  `test_scripting` 27/27，均 exit 0；`git diff --check` 0；残留进程 0。
- `Bind()/Clear()` 的静默提前返回（`!IsOwnerThread() || !glfwGetCurrentContext()`）
  仍仅作为**隐患记录**，未并入本次修复（未触发，扩展会越界）。

### 5.22 B2-L2 逐路径源码定位（冻结）：**FAULTED `Shutdown()` 路径违反销毁顺序 ⇒ B2 定位**

纯读码，**未做任何 instrumentation**（B2-L1 的 seam 已证明不可靠，不再重演）。

#### 5.22.1 关键所有权事实：`Application::Shutdown()` **不释放** shader 与 batch

```cpp
void Application::Shutdown() {
    if (m_ShutdownComplete) return;
    if (m_Initialized) { m_SubsystemManager.Shutdown(); Time::Shutdown(); m_Initialized = false; }
    m_ShutdownComplete = true;
    m_Factory.SetAllocator(nullptr);
    if (s_Instance == this) s_Instance = nullptr;
    g_SubsystemAllocator = nullptr;
}
```

它**从不触碰** `m_ProductionSpriteShader`（GL program）、`m_ProductionSceneRenderer`（→ batch）。

| 对象 | 持有者 | GL 名字何时被删除 |
|---|---|---|
| `sprite_batch` program | `Application::m_ProductionSpriteShader`（`shared_ptr`） | 仅 `~OpenGLShader` → `glDeleteProgram` |
| batch VAO/VBO/EBO | `Application::m_ProductionSceneRenderer` → `ISpriteBatch`（`shared_ptr`） | 仅 `~OpenGLSpriteBatch` → `glDeleteVertexArrays/Buffers` |

⇒ 二者**只在最后一个 `shared_ptr` 引用释放时**删除，即
**良路径 = `m_Application.reset()`；异常路径 = 隐式成员析构**。

#### 5.22.2 逐路径核对

| 路径 | `Application::Shutdown()` | batch/shader 释放 | `m_Application.reset()` | `m_RenderContext` 仍存活 | context current |
|---|---|---|---|---|---|
| 正常 `Shutdown()` | ✅ `:629` | 随 reset | ✅ 显式 `:630` | ✅ 到 `:639` 才销毁 | ✅ 先 `MakeRuntimeContextCurrent()` |
| 紧急（`RuntimeLoop:451` → `PerformEmergencyTeardown`，**runtime 线程**） | ✅ | 随 reset | ✅ 显式 | ✅ | ✅ `MakePersistentContextCurrent()` |
| bootstrap 回滚（`InitializeRuntimeResources` catch，`:566/:574`） | ✅ | 随 reset | ✅ 显式 | ✅ | ✅ 命令在 context current 下执行 |
| **`Shutdown()` 的 FAULTED 分支** | ❌ **从不调用** | ❌ **从不** | ❌ **无显式 reset** | ❌ **已被销毁** | ❌ **完全没有 context** |
| `CloseProject`（不销毁 runtime） | n/a | n/a | n/a | n/a | 非销毁路径，不违反 |
| `~EngineHost` → `Shutdown(nullptr)` | 依状态路由；faulted 时落入上一行 | 同上 | | | |

#### 5.22.3 违反路径的完整链条

```cpp
// Shutdown() 的 FAULTED 分支
if (m_State.load() == ENGINE_HOST_STATE_FAULTED) {
    RequestEmergencyStop();
    if (!waitAndJoinRuntime()) { ... return -1; }
    const bool graphicsTeardown = TeardownGraphics();   // ← 只拆 graphics，不拆 runtime 资源
    ...
    return -1;
}

// TeardownGraphics()：只做这两件事
success = m_GraphicsFactory->ShutdownPersistentContext();   // ← 销毁 GL context / GLFW
if (success) m_GraphicsFactory.reset();                    // ← 销毁 factory
```

随后 `~EngineHost` 结束，成员按**声明逆序**隐式析构：

```cpp
// EngineHost.h 声明顺序
253 m_GraphicsFactory;  254 m_Application;  255 m_RenderContext;
256 m_OffscreenFbo;     258 m_PresentationFbo;  260 m_Session;
```

逆序 ⇒ `m_Session`(260) → `m_PresentationFbo`(258) → `m_OffscreenFbo`(256) →
**`m_RenderContext`(255)** → **`m_Application`(254)** ⇒
**GL context 先死，Application（含 batch 与 program）后死。**

而 `~OpenGLSpriteBatch` / `~OpenGLShader` **没有任何守卫**（对比 `~OpenGLFramebuffer` 有
`IsOwnerThread() && glfwGetCurrentContext()` 保护），于是
`glDeleteVertexArrays` / `glDeleteBuffers` / `glDeleteProgram`
**在没有 current context 的情况下被调用**。

**与观测现象的吻合**：门控打开后，生产渲染每帧执行 → §3.2 记录过"该路径泄漏 GL 状态，
使下一帧 64×64 探针的 clear-color 校验失败" → 触发
`FillRejectedResult(GL_FAILED, STAGE_FBO, "offscreen FBO readback color mismatch")`
→ host 进入 **FAULTED** → 销毁时走 FAULTED 分支 → 在 context 已拆之后做无守卫 GL 删除
→ GLFW/上下文状态被破坏 → **同进程下一个 host 的 `glfwInit()` 失败**
（观测到的 `failed to initialize GLFW`，且只在生产渲染真的跑过之后出现、且与顺序相关）。

#### 5.22.4 顺带排除的线索

`m_SubsystemAllocator` 声明在生产成员**之前**（`Application.h:231` vs `:280-284`），
逆序析构 ⇒ 生产成员先死、分配器后死 ⇒ **析构时分配器仍存活**。
因此"256KB 池被跨 host 不重置"**不能**解释本条路径，**不作为 B2 机制**（仍非无害，但非此处根因）。
`GL_SCISSOR_BOX=[0,0,120,64]` 遗留值同样**未被证明**与 B2 相关。

**B2-L2 判定：CLOSED as localization** —— 已定位到一条具体违反顺序的路径
（FAULTED `Shutdown()` → `TeardownGraphics()` → 隐式 `m_RenderContext` 先于 `m_Application` 析构
→ 无守卫 GL 删除）。**尚未打补丁**（无守卫析构只是潜在修法，不是证据）。

### 5.23 B2 修复切片 (a)+(c)（冻结）：**部分验收通过，但 #6 未达成 ⇒ B2 未关闭**

按批准范围实施 **(a) + (c)**，**(b) 明确不实施**（不向 `Application::Shutdown()` 搬移所有权）。

#### 5.23.1 实施内容

**(a) FAULTED 路径先拆 runtime 资源、再拆 graphics**

```cpp
if (m_State.load() == ENGINE_HOST_STATE_FAULTED) {
    RequestEmergencyStop();
    if (!waitAndJoinRuntime()) { ... return -1; }
    // 不假设 join 之后 context 仍然 current —— 显式设为 current
    const bool contextCurrent = m_GraphicsFactory && m_GraphicsFactory->MakePersistentContextCurrent();
    bool runtimeTeardown = contextCurrent ? TeardownRuntimeResources() : false;
    if (contextCurrent && !m_GraphicsFactory->UncurrentPersistentContext()) runtimeTeardown = false;
    const bool graphicsTeardown = TeardownGraphics();     // ← 现在排在 runtime 拆除之后
    ...
}
```

**(c) 两个析构补齐与 `~OpenGLFramebuffer` 同款的双重守卫**
（`OpenGLSpriteBatch` / `OpenGLShader` 各新增 `m_OwnerThreadId`，构造时记录，析构时
`IsOwnerThread() && glfwGetCurrentContext()` 才发 `glDelete*`，否则放弃删除）。
未改动 batch 的**渲染**逻辑，未改 C ABI / P2 / HRC-2 pump/ownership。

#### 5.23.2 验收逐条

| # | 项 | 结果 | 证据 |
|---|---|---|---|
| 1 | 正常 A6 生命周期 GREEN | ✅ | 门控关闭态 41/41 |
| 2 | FAULTED 路径先释放生产 GL 资源再拆 graphics | ✅（代码序） | `TeardownRuntimeResources()` 先于 `TeardownGraphics()`，且显式建立 current context |
| 3 | 析构不在无属主线程/current context 下发 `glDelete*` | ✅ | 两个守卫已就位；不再存在无守卫删除路径 |
| 4 | 生产渲染仍 **7296** non-clear | ✅ | `[B2ACC] hostA nonClear=7296 renderables=9 cycles=3` |
| 5 | draw-FBO 断言连续 3 帧 GREEN | ✅ | 同上，`cycles=3`，三次 `PumpOneFrame` 均返回 0 |
| 6 | `RuntimeSeamProofThreeCycles` **在全量套件中**通过 | ❌ **未达成** | 门控 ON 全量：`[ PASSED ] 40` + `[ FAILED ] RuntimeSeamProofThreeCycles` + `TEMP_B2Acceptance`，exit 1 |
| 7 | Host A → Host B 同进程成功 | ⚠ **单例通过、套件内不通过** | 单独运行：`hostA shutdown=0 hostBReady=1`、`hostB pump=0`；但放进全量套件即失败 |
| 8 | `test_bridge` 41/41 + 1 disabled（Debug+Release） | ✅（门控关闭态） | 均 41/41 + 1 DISABLED、exit 0 |
| 9 | `test_hrc` 15/15、`test_scripting` 27/27 | ✅ | GREEN |
| 10 | C ABI 未变、残留 0、`diff --check` 干净 | ✅ | 无导出/结构体改动；残留 0；exit 0 |

**裁决：验收在 #6 失败 ⇒ B2 未关闭，§5.22 的定位只覆盖了机制之一，仍存在第二个机制。**
门控保持 `false`，**P1.1 仍 OPEN**。(a)+(c) 作为已批准的加固保留在代码中。

#### 5.23.3 一条必须记录的机制性发现：同进程只能有一个存活 `EngineHost`

失败日志里 `RuntimeSeamProofThreeCycles` 自身 `OpenGL 4.6 loaded` 成功、随后又出现
`failed to initialize GLFW`。追查发现 **hidden persistent context 是进程级的**：
**同一进程同时只能有一个存活的 `EngineHost`**。

这也**纠正了 B2-L1 的"零 trace"**：当时的探针额外持有一个 host，导致 host A 的
`glfwInit()` 直接失败 ⇒ host A 从未建立图形、从未渲染、从未拆除 ⇒ `teardownSteps=0`。
**零 trace 是我的测试脚手架缺陷造成的，不是拆除顺序的证据。**
（该结论不改变 §5.22 基于**源码**的顺序分析。）

#### 5.23.4 剩余待定位（不得在无证据前修补）

- 门控 ON 时，前一个 host 的生产渲染结束后，**同一进程内下一个 host 仍可能 `glfwInit()` 失败**；
  `RuntimeSeamProofThreeCycles` 全量失败即此现象。
- 候选（**均为假设**）：presentation FBO / batch / shader 的 GL 对象在**同进程跨 host** 生存或
  重复删除；`StackAllocator` 池跨 host 不重置导致 256KB 预算被逐 host 消耗；
  `ShutdownPersistentContext()` 与仍存活的 GL 对象之间的交互。
- 下一切片应针对"**跨 host 的 GL 对象与 GLFW 全局状态**"，而不是继续在单 host 内部加守卫。

### 5.24 三路跨 host 探针（冻结，仅 instrumentation 之外的既有 ABI）：**#6 失败点是"同 host 下一帧"，不是跨 host GLFW**

用**既有** C ABI（`EngineHost_GetGlfwLifetimeState` + `RuntimeResult.stage/diagnostic`）做**严格串行**
A → shutdown → destroy → B，**未新增任何生产代码**。

```text
[P1] A alive:            glfw users=1 initCount=1 terminateCount=0
[P2] A after render:     glfw users=1 initCount=1 terminateCount=0
[P3] A after shutdown:   rc=0 status=8 stage=10 glfw users=0 initCount=1 terminateCount=1
[P4] B create:           handle=OK status=0 stage=0 errorCode=0
[P4] B open+pump:        pump=0
[P4] B alive:            glfw users=1 initCount=2 terminateCount=1
```

#### 5.24.1 判定：GLFW 全局生命周期**不是**本例机制

`terminateCount` 0→1、`users` 1→0、`initCount` 1→2 ⇒ **A 的 shutdown 干净地 terminate 了 GLFW，
B 重新 init 成功并成功 pump**。因此用户判定标准里的
"A shutdown → GLFW persistent state inconsistent → B fails before GL object creation"
**不成立**；"A shutdown → GL object still alive → B fails" 同样**不成立**（B 正常建图形）。

#### 5.24.2 #6 的真实失败点：**同一 host 内，下一帧的 64×64 探针校验**

门控 ON、排除我自己的 TEMP 用例后跑全量：`[ PASSED ] 40` + `[ FAILED ] RuntimeSeamProofThreeCycles`。
失败断言定位到 `test_editor_capi.cpp:529`：

```text
test_editor_capi.cpp(529): error: Expected equality of these values:
  EngineHost_PumpOneFrame(host, 0.016f, &result)
  Which is: -1
  0
```

该用例在 `EngineHost_Play(host, -1, ...)` 之后连续 pump 3 帧。`PumpOneFrame = -1` 对应的正是
`EngineHost.cpp` 里 `"offscreen FBO readback color mismatch"` 那条 typed failure：
**生产渲染路径污染了紧接着的下一帧，使 64×64 探针的 clear-color 校验失败。**
这与 §3.2 最早记录的 B2 现象**完全一致**，而**不是**跨 host 的 GLFW 初始化问题。
（套件里出现的 2 次 `failed to initialize GLFW` 属连带/顺序效应，不是本例的首个失败点。）

#### 5.24.3 由此得到的定位修正（对我自己 §5.22 结论的再修正）

- §5.22 定位的 **FAULTED teardown 顺序缺陷是真实的**，修复 (a)+(c) 也确实消除了**严格串行 A→B** 的失败
  （5.24.1 为正面证据）。这一点成立。
- 但 **B2 的主症状不是"跨 host GLFW 被污染"，而是"生产渲染路径在同一 host 内泄漏状态，
  使下一帧 64×64 探针读回校验失败"**。`PumpOneFrame = -1` 才是 #6 的首个确定性失败点。
- 因此**下一刀应针对"生产渲染路径每帧结束后残留了哪些 GL 状态"**，
  而不是继续做跨 host GL 对象生命周期或 GLFW 全局生命周期的修复。

#### 5.24.4 清理与复核
临时探针用例已删除（残留 0），**未新增任何生产 instrumentation**（全部用既有 C ABI）。
门控恢复 `false`；(a)+(c) 保留。复核：`test_bridge` **41/41 + 1 DISABLED**（Debug + Release）、
`test_hrc` 15/15、`test_scripting` 27/27、均 exit 0；`git diff --check` 0；残留 0；C ABI 未变。

### 5.25 下一步的有界选项

| 选项 | 内容 | 边界 |
|---|---|---|
| **B2-L3（建议）** | 只诊断**生产渲染路径返回后**残留的 GL 状态：对比"跑过生产渲染的帧"与"未跑的帧"在**同一 probe 校验点**上的状态差异（scissor/cull/depth/blend/colorMask/viewport/framebuffer/program/VAO/active texture）。用既有 ABI + 一次只读观测，**不改行为** | 不加新 guard、不改 `OpenGLSpriteBatch`、不碰 C ABI / P2 / HRC-2 |

### 5.26 下一步的有界选项


| 选项 | 内容 | 边界 |
|---|---|---|
| ~~**A1（诊断）**~~ | ~~临时诊断渲染路径~~ | ✅ **已完成**：`magenta=86400` 精确命中，基础设施无问题（见 §5.5） |
| **A2（已执行）** | 真实 batch payload → 诊断光栅路径 | ✅ **已执行**：payload 提取成功（9 sprites），但诊断自身 setup 抛 `0x502` ⇒ **A2 问题未回答**，证据已冻结（见 §5.7） |
| **A3（已执行）** | 设置序列逐步二分 + A1 control | ✅ **已执行**：第一个非法调用 = 步骤 16 `glUniformMatrix4fv`；步骤 0–15 全清；control 仍 86400。**根因是诊断漏了 program 激活，生产路径无此缺陷**（见 §5.8） |
| **A2 重跑（已执行）** | 冻结 A2 + 唯一修正（`UseProgram` 先于 `UniformMatrix4fv`） | ✅ **已执行**：`payload=91656` 片元、`control=86400` 不变、全程 `0x0`。**payload 可渲染** ⇒ B1 收窄到 batch 自身 GL 处理（见 §5.9） |
| **A5（已执行）** | C1 管线状态逐项隔离 | ✅ **已执行**：五个变体输出完全一致（magenta 相同、四个状态实测全 0）⇒ **C1 排除**。两个弱点已在 §5.10 写明（"释放"为空操作、control 未逐变体清空） |
| ~~**A6（下一候选，唯一）**~~ | ~~C2 纹理采样~~ | ✅ **已执行**：control / A6-1 / A6-2 三者均 6,552、`0x0` 无错 ⇒ **C2 排除**；采样 uniform 不对称**非缺陷**（默认 0 与 `Bind(0)` 一致）。并**修正** A2R 数字：payload 自身约 6,552（见 §5.11） |
| ~~**A7（下一候选，唯一）**~~ | ~~直接观测生产 `OpenGLSpriteBatch`~~ | ✅ **已执行**：真实 batch **能出片元**（fitted 7,296 / identity 120,000，`spriteCounts=1,4,4`，六阶段 GL 全 `0x0`）⇒ **B1 回到我方 production render step**；并**证伪 C3/C4**（见 §5.12） |
| B | 换 3D 内容路径 | 已判定无证据优势（3D 路径同样无证明） |
| C | P1 按"结构层"收口，内容断言后置 | 已被你否决（会降级 D1 目标） |

