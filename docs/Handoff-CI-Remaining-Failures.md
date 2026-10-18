# 交接：CI 全量套件的剩余失败

**最新证据**：run `39074497601`（`1d3b27e`）—— **7 个 job 全绿**，四个构建 job
均为 `100% tests passed out of 13`。

| job | 结果 |
|---|---|
| `ubuntu-latest` Release gcc / clang | **13/13 通过** |
| `windows-latest` Release cl | **13/13 通过** |
| `windows-latest` RelWithDebInfo cl **ASan=ON** | **13/13 通过**（此前从未跑到过这一步） |
| `gl-capability-probe` / `vulkan-backend-compile` / `jolt-link-diagnose` | 成功 |

**像素验证不再是缺席状态**：Linux runner 上 4 个蒙皮像素用例中的 3 个用 Vulkan
（lavapipe）真实执行了像素断言，这是它们历史上第一次在 CI 里真正运行。证据见 §7。

下文 §1–§3 是本文最初的 3 项 Linux 失败，已由 AssetBakery fixture 生成器关闭；
§4 已由「把 ASan 运行时放进 PATH」关闭；§5 与 §6 已关闭；§7 记录像素验证本次落地
的范围与仍未覆盖的部分。

**仍未关闭的是像素验证本身**：四个 runner 上它一次都没有真正执行过，全部是显式
SKIP。CI 绿指的是「其余一切通过」，不是「GPU 像素已验证」。终点见 §5 方案 2。
（本行写于方案 2 落地之前；现状见 §7。）

---

## 1. ~~`test_gp01` —— 引用从未入库的资产（段错误）~~ 已关闭（`37b8543`）

### 现象

```
GP01Harness.h:142: Failure
  Value of: LoadSnapshotFromFile(scenePath, snap, err)
  Actual: false / Expected: true
  cannot open file: assets/gp01/Main.scene
```

`LoadSnapshotFromFile` 失败后用例继续执行，`InstantiateScene` 在空快照上运行，
进程随后崩溃（`***Exception: SegFault`）。

### 根因：资产不存在

```
assets/gp01/Main.scene    tracked=0  history=0
assets/gp01/manifest.json tracked=1
```

`assets/gp01/Main.scene` **从未进入任何分支**。同目录的 `manifest.json`、`game.lua`
和 `tex/` 下贴图都在，唯独缺 `Main.scene`。

### 关闭方式

`AssetBakery --gp01-scene <manifest> <out.scene>`（`37b8543`）用**已入库的**
`manifest.json` 生成该场景，GUID 按路径从 manifest 解析，序列化走引擎自己的
`SceneSerializerV1`，因此不可能与加载器漂移。工作流在跑测试之前调用它
（`68ae12b`），干净 checkout 自此自给自足。`test_gp01` 22/22、`test_bridge` 28/28 通过。

### 一个已排除的假设

曾怀疑是「ctest 工作目录是构建输出目录，仓库相对路径解析不到」。**该假设错误**：
`ctest` 的 working-directory 确实是构建目录，但 `GL46DeviceTest.cpp::ResolveShaderPath`
早就为此做了向上遍历定位仓库根的处理；真正原因是文件本身不在仓库里。
曾按该假设改写 13 处调用点并新增 `tests/utils/TestAssets.h`，实测无效后已整体回滚
（`63f1725`）。

> 原先的待办是「请 owner 确认 `Main.scene` 是遗漏提交还是用例指错文件」。
> 答案由 `37b8543` 给出：它是遗漏提交，用生成器补齐，而不是改用例指向。

---

## 2. ~~`test_content` —— 同类资产缺失（非段错误）~~ 已关闭（`1e794a3`）

### 现象

60 个用例全部执行完成（早期是段错误，已修复），仅剩断言失败：

```
ContentPipelineTest.cpp:583 / :645  reg.LoadManifest("assets/scenes/dogfood03.manifest.json") → false
Dogfood05Test.cpp:30                reg.LoadManifest("assets/scenes/dogfood05.manifest.json") → false
Dogfood06Test.cpp / Dogfood07Test.cpp 同类
```

### 根因：与第 1 项相同

```
assets/scenes/dogfood03.scene           tracked=0  history=0
assets/scenes/dogfood03.manifest.json   tracked=0  history=0
assets/scenes/dogfood05.manifest.json   tracked=0  history=0
assets/scenes/dogfood01.scene           tracked=1  history=1   ← 仅这一个入库
```

`assets/scenes/` 下共只有 `dogfood01.scene` 与 `scene_groups.json` 两个文件，
而 Dogfood03/05/06/07 四组用例都要求各自的 scene 与 manifest。

### 关闭方式

`AssetBakery --dogfood-scenes <dir>`（`1e794a3`）补上 dogfood03/05/06/07 的
scene + manifest。`test_content` 60/60 通过。

---

## 3. ~~`test_bridge` —— 属 HRC 所有权范围~~ 已关闭（`37b8543`）

`bridge/` 与 `tests/test_bridge/test_editor_capi.cpp` 均为 **HRC-OWNED**（主工作树中
存在未提交改动），本轮工作全程未触碰。

当时的段错误与 CAPI 桥接本身无关：同一次运行里 `test_gp01` 也崩在
`cannot open file: assets/gp01/Main.scene` 上，`test_bridge` 用例同样需要该场景，
而它在干净 checkout 里不存在。场景由 §1 的生成器补齐后，`test_bridge` 28/28 通过，
未改动任何 bridge 代码。

---

## 上一轮（§1–§3 之前）的对照，供确认无回归

| 测试 | 修复前 | 修复后 |
|---|---|---|
| `test_renderer` | 失败 | 通过（4 个 GPU 用例显式 SKIP） |
| `test_io` | 失败 | 通过 |
| `test_physics` | 失败 | 通过 |
| `test_content` | **段错误** | 普通断言失败 |
| Linux `all` 构建 | 45 个 target 中止 | 68 个 target **通过** |

Linux 套件从 **54%（6 失败）** → **77%（3 失败）** → 本轮 **13/13 全通过**
（fixture 生成器落地后）。

---

## 4. Windows RelWithDebInfo + ASan —— 该 job 里**没有一个产物能启动**

### 现象

`481d202` 之前停在「Generate scene fixtures」并报找不到 AssetBakery；`481d202` 用
`GITHUB_WORKSPACE` + `find` 修好路径之后，该步骤**确实找到了**产物：

```
using bakery: build/sandbox/AssetBakery/RelWithDebInfo/AssetBakery.exe
##[error]Process completed with exit code 127.
```

127 后面**没有任何错误信息**。更早的一次运行（`38984032150`）里，同一个 job 的
「Verify SPIR-V activation」步骤给出了同源的另一副面孔：

```
OK  SpirVTest: ...\build\sandbox\SpirVTest\RelWithDebInfo\SpirVTest.exe
Exception: SpirVTest failed with exit -1073741515
```

### 根因：MSVC 的 ASan 运行时是**动态**的，而 runner 的 PATH 里没有它

`/fsanitize=address` 产出的可执行文件依赖 `clang_rt.asan_dynamic-x86_64.dll`
（`dumpbin /dependents` 可见），该 DLL 只存在于 VS 工具链的
`VC\Tools\MSVC\<ver>\bin\Hostx64\x64\` 下。GitHub 的 Windows runner 的 PATH 里没有
这个目录，于是 CreateProcess 以 `0xC0000135`（`STATUS_DLL_NOT_FOUND`）失败。

**两种呈现是同一件事**：`-1073741515` 就是 `0xC0000135`；MSYS 的 bash 把它映射成
`ENOENT`，于是报 exit 127 且不打印任何消息。

本机实测（VS 2022 17.14 / Git Bash），同一个 ASan 产物：

| 环境 | pwsh | Git Bash |
|---|---|---|
| PATH 含工具链目录 | 正常启动 | 正常启动 |
| PATH 不含 | `-1073741515` | `127`，无输出 |

### 为什么上一次的修复没能解决它

`481d202` 修的是**路径**（Windows 反斜杠被 bash 当转义符吃掉），那个诊断是对的：
`using bakery:` 这行证明路径问题已解决。但它无法解释 127 —— 于是很容易把 127 读成
「路径还是不对」，继续在 find/引号上打转，而真正的缺失在 PATH 里。

> 教训：`127` 在 bash 里最常见的含义是「命令不存在」，但它也会掩盖「进程创建失败」。
> 这一步的失败信息里没有一行提到 DLL，因此只能靠同 job 里另一个产物的
> `-1073741515` 才能把两件事联系起来。**同一次运行里不同步骤的失败应当一起读。**

### 修复

在 Build 之后加一步，把 `clang_rt.asan_dynamic-x86_64.dll` 所在目录写入
`GITHUB_PATH`（仅 `runner.os == 'Windows' && matrix.enable_asan == 'ON'` 时执行，
因为非 ASan 产物不依赖它）。该 job 由此第一次真正跑到 `Test (full suite)` ——
在此之前它从未执行过任何一个编译出来的产物。

### 尚未验证的部分

本机 ASan 产物在有 DLL 时启动成功，但紧接着报
`AddressSanitizer: CHECK failed: interception_win.cpp:193`（需
`ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1`，`tests/CMakeLists.txt` 已为测试套件设置）。
该 interception 失败与本机 CPU 有关，runner 是否命中尚未观测。若命中，症状会是
「跑起来了但退出码非 0」，届时再按证据处理，不在本次一并加开关。

---

## 5. ~~Windows Release —— `test_renderer` 需要 runner 给不出的 GL 4.6~~ 已按方案 1 关闭

Windows Release 是 12/13，唯一失败的是 `test_renderer`，4 个
`GPUSkinningPixelTest` 用例：

```
[FATAL] GL 4.6 core context creation failed
GPUSkinningPixelTest.cpp:355: no GL 4.6 core context; GPU skinning verification cannot be skipped
```

这与 Linux 的情况**不是同一件事**，程度更重：

| runner | 探测结果 |
|---|---|
| Linux（llvmpipe） | 4.5 core 可创建，4.6 core 创建失败 → 能力不足 |
| Windows | `[GLPROBE] default context: CREATE FAILED` → **连默认上下文都建不出来** |

Linux 测试步骤声明 `GPU_SKINNING_NO_GL46=1`，Windows 侧此前没有声明，因此按设计失败。
两条候选路线，本轮选了第 1 条：

1. **与 Linux 对齐（已采用）**：Windows 测试步骤同样声明 `GPU_SKINNING_NO_GL46=1`。
   这是既有 opt-in 机制，能力证据由 `gl-capability-probe` job 持续采集。
   **代价要说清楚**：Windows 上「像素验证没跑」不再表现为红灯，改由 `Report GPU
   skinning verification status` 步骤打印的 `GPU-SKINNING-STATUS: NOT VERIFIED` 承担
   （该步骤 `if: always()`，且不因此让 job 变红，与 Linux 行为一致）。
   真正变窄的是：Windows 上从此不再有任何用例会因为「GL 用例跑不起来」而失败，
   一次 GL 环境彻底损坏将表现为 SKIP 而不是红灯。
2. **把像素验证做成后端无关**（`docs/Handoff-Vulkan-Headless-GPU-Verification.md` §4.8，
   未做）：4 个用例改走 Vulkan/lavapipe——那条路径已在 CI 中拿到真实像素证据并常开
   验证。做完之后 `GPU_SKINNING_NO_GL46` 这个开关本身就可以删掉，第 1 条的代价随之
   消失。这是有实质工作量的一条，也是这条退路的终点。

---

## 6. ~~Windows 上「GPU 蒙皮状态报告」步骤静默退化~~ 已关闭（`174150c`，run `39023923581`）

run `39016478050` 全绿之后，两个 Windows job 的 Report 步骤打印的是：

```
GPU-SKINNING-STATUS: tests/test_renderer not found; nothing was verified
```

Linux 打印的则是预期中的：

```
GPU-SKINNING-STATUS: NOT VERIFIED on this runner (explicit skip above).
GPU-SKINNING-STATUS: pixel verification requires a GL 4.6 capable machine.
```

原因是该步骤硬编码了 `BIN=tests/test_renderer`，而多配置生成器（Windows/MSVC）
把可执行文件放在 `tests/<Config>/test_renderer.exe`。这与 `481d202`
在 fixture 步骤里修掉的是**同一个坑**：那次是路径被 bash 的转义处理吃掉，这次是
布局假设错误。

**为什么值得单独修**：这一存在的唯一理由，是让「像素验证没跑」在绿色对勾之外仍然
显式可见。§5 采纳方案 1 之后，Windows 上四个用例不再失败，这份证据就成了**仅剩的
承载点**——而它恰恰在 Windows 上不产出任何东西。改用 `find` 后，run `39023923581`
两个 Windows job 恢复为：

```
using test_renderer: tests/Release/test_renderer.exe
[  SKIPPED ] GPUSkinningPixelTest.BindPoseDrawsGeometryInCentreBandOnly (0 ms)
...（4 条）
GPU-SKINNING-STATUS: NOT VERIFIED on this runner (explicit skip above).
GPU-SKINNING-STATUS: pixel verification requires a GL 4.6 capable machine.
```

> 教训：一个用 `exit 0` 兜底、且兜底文案与真实结论同形的步骤（"nothing was
> verified"），失败时不会引起任何人注意。**兜底分支应当与成功分支长得不一样。**

---

## 7. 蒙皮像素验证落地：Linux 上真实执行（`630bfa3` / `1d3b27e`，run `39074497601`）

```
[skin-pixel] backend: Vulkan (llvmpipe (LLVM 20.1.2, 256 bits))
[       OK ] GPUSkinningPixelTest.BindPoseDrawsGeometryInCentreBandOnly (5 ms)
[       OK ] GPUSkinningPixelTest.TranslatedBoneMovesGeometryIntoRightBand (0 ms)
[       OK ] GPUSkinningPixelTest.SkinnedProgramIsInUseAndBackgroundIsStable (0 ms)
[  SKIPPED ] GPUSkinningPixelTest.ProductDrawBranchChangesPixelsWithPose (0 ms)
GPU-SKINNING-STATUS: ran for real on this runner.
```

这是这四个用例**第一次**在 CI 里真正跑起来。此前它们在每一个 runner 上都是 SKIP：
纯 GL 实现（自建 GLFW 4.6 窗口 + 私有 FBO + `glReadPixels`），而引擎要求 GL 4.6。

### 「后端无关」做不到「一份代码两个驱动」，这里做的是别的

GL 后端的 `IRHICommandList` 光栅化路径**全是空桩**：`SetVertexBuffer` /
`SetIndexBuffer` / `Draw` / `BeginRendering` / `Submit` 全是 `{}`，`CreateTexture`
返回空纹理。GL 的渲染完全走另一套抽象（`IGraphicsFactory` / `IRenderContext`）。
因此真正做成「同一份代码跑两个后端」需要先补齐 GL 的 IRHI 光栅化，那是另一件
量级的事。

本次落地的是：**用例、顶点数据、骨骼矩阵、分带分析、断言只写一遍**（1–3 号用例
的函数体在 `SkinningPixelCases.cpp`，GL 与 Vulkan 两个后端跑的是同一段代码），
像素怎么弄出来由后端各自负责。后端挑选顺序为 GL 4.6 → Vulkan；两者都没有时
fixture 报出**两次尝试各自的实测原因**。

### 各后端必须显式处理的地方

| | GL | Vulkan |
|---|---|---|
| 着色器 | 产品 `skinned_lit.*`（GLSL 330 + 松散 uniform） | 自带 GLSL 450，uniform 放进 UBO，shaderc 编译成 SPIR-V |
| 属性 | 声明 location 0..5（产品着色器要求） | 只声明用得到的 3 个并在着色器里重新编号 |
| 骨骼索引格式 | `ivec4` | `RGBA32_SInt` —— 该映射此前缺失（`606b024` 补上） |
| 回读朝向 | 自下而上 | 自上而下，**翻行**后再交给共享断言 |
| 剔除 | 默认不剔除 | `VulkanPipelineState` 硬编码 `cullMode=BACK` + `frontFace=CLOCKWISE`，绕序有约束 |

`FormatToVk` 的缺口是本次撞出来的：`RGBA16_UNorm/SNorm/UInt/SInt` 与
`RGBA32_UInt/SInt` 全部落到 `default` 返回 `VK_FORMAT_UNDEFINED`，而 0 是**非法值**
不是「未知」，于是会建出非法管线。与此前 `RG32_Float` 是同一类缺陷，因此一次补齐
整批而不是只补 `RGBA32_SInt`。

### 仍未覆盖的部分

1. **第 4 个用例仍是 GL 专属**（已实测工作量，见下）。
2. **Windows 上的后端**（已实测，见下）。
3. **`GPU_SKINNING_NO_GL46` 因此保留，但只对 Windows 生效。** Linux 侧已撤掉：
   留着会把「Vulkan 建不起来」变成一次静默跳过，正好盖住最需要被发现的回归。

### 未覆盖项 1 的实测结论：这是多周的功能移植，不是测试改动

第 4 个用例验的是 `Engine::DrawSkinnedMeshIndexed`
（`engine/include/Engine/Animation/SkinnedMeshDraw.h:38-43`），全仓库只有两个调用者：
产品路径 `sandbox/src/EditorDemo/EditorDemoApp.h:415` 与本用例的 GL 后端。

**阻塞点是签名，不是函数体**：

| 产品侧的东西 | Vulkan 侧的对应物 | 状态 |
| --- | --- | --- |
| `Shader&` + `Bind()` + `SetMat4/SetMat4Array`（按名字寻址） | `IRHICommandList::SetPipelineState` + `SetConstantBuffer(set,binding,buf,...)` | 语义不同：GL 是每次绘制按名上传，Vulkan 是**常驻 UBO**，其生命周期是一个设计决定，当前 API 无处表达 |
| `VertexArray&` + `VertexAttribute{location,size,stride,offset}` | `GraphicsPSODesc::vertexInput`（每个属性要显式 `Format`） | 模型形状不同；骨骼索引还要 `RGBA32_SInt`（本次才补上） |
| `IRenderContext` / `IGraphicsFactory` / `IWindow` | 无 | **Vulkan 一个都没有实现**，全仓库只有 `OpenGL*` 实现这三个接口 |
| `skinned_lit.vert`（GLSL 330 + 松散 uniform） | 需要 `#version 450` + `std140` UBO | 松散 uniform 是**刻意**选的（GL 里 block 成员按名寻址返回 -1），SPIR-V 无此概念 |
| GL 的 `IRHICommandList` 光栅化路径 | —— | 全是空桩，两套栈**在绘制这一层根本没接上**，没有可搬过去的共同抽象 |

结论：需要动的文件至少有 `SkinnedMeshDraw.h`（改签名，破坏两个调用者）、
`EditorDemoApp.h`（`BuildSkinnedMeshResource` / `RenderSkinnedGameObject` / 同函数的
非蒙皮分支）、`RenderResources/{Shader,VertexArray,VertexBuffer,IndexBuffer}.h`、
`EngineEditor.cpp` + `ViewportPanel.cpp`（视口 FBO 是 raw GL），
以及**三个尚不存在的 Vulkan 后端类**（Vulkan 版 `IRenderContext`/`IGraphicsFactory`/
`IWindow`）。按本仓库自己的说法（`RHIDeviceFactory.h:7-9`：Vulkan 后端「只能被
sandbox 测试触及」），这就是那件事本身。

**本轮为什么不做**：给 Vulkan 造一个「产品绘制分支」替身、只让用例去调它，
等于验证一个生产代码不调用的函数——那比显式跳过更糟（它会把「没人用的抽象是对的」
写成「产品路径已验证」）。因此选择显式跳过并写明理由。

**本轮实际做的替代动作**：把 Vulkan 蒙皮着色器从测试内联字符串移到
`assets/shaders/skin_pixel.{vert,frag}`，并在那两个文件的头注释里写下「为什么不能
直接复用产品 skinned_lit.*」——这是移植要跨过的第一道门，写在代码里而不是只写在
交接文档里。

### 未覆盖项 2 的实测结论：Windows 需要装一个软件 Vulkan 栈

Windows runner 给不出 GL 4.6（实测连默认上下文都 CREATE FAILED），也没有任何软件
Vulkan 驱动（CI 从不安装 Vulkan SDK，`find_package(Vulkan)` 恒失败 →
`ENGINE_HAS_VULKAN` 未定义 → Vulkan 后端在 Windows 上**从未被编译过**）。

可行拼图有两块，缺一不可：

| 阶段 | 需要什么 | 来源 |
| --- | --- | --- |
| 编译期 | 头文件 + `vulkan-1.lib` | LunarG Vulkan SDK（choco 包 `vulkan-sdk`）。`FindVulkan` 认 `$ENV{VULKAN_SDK}` |
| 运行期 | 一个真的 Vulkan 设备 | Mesa **lavapipe**（CPU 版 Vulkan 驱动），`mesa-dist-win` 有 Windows 预编译包：`vulkan_lvp.dll` + `lvp_icd.x86_64.json`，由 loader 经 ICD 发现机制加载 |

两条都不便宜：SDK 是几百 MB 的第三方安装，lavapipe 是第三方 7z 包。**因此先测量再
接线**——新增的 probe 步骤只打印 `WINDOWS-VULKAN-CAPABILITY:` 一行，不 gate 任何用例；
像素验证是否改用 Vulkan，由这一行的实测结果决定，而不是先改声明再祈祷它能跑。

顺带一个此前没人提过的事实：装上 SDK 之后，Windows 会**第一次真正编译并链接 Vulkan
后端**。这既可能让 Windows 构建出问题（此前该后端被完全排除，`5586e16` 的链接错误
正是这么被掩盖的），也是 Windows 上第一次对它有编译级覆盖。

---

## GPU 像素验证的独立限制（历史记录，见 §7 为现状）

4 个 `GPUSkinningPixelTest` 用例**在 Windows runner 上**不会真正执行像素验证，
Linux 上已由 §7 解决。Windows 侧 CI 输出：

```
GPU-SKINNING-STATUS: NOT VERIFIED on this runner (explicit skip above).
GPU-SKINNING-STATUS: pixel verification requires a GL 4.6 capable machine.
```

原因：引擎要求 OpenGL 4.6，而该 runner 能力不足，且已穷尽：

| 驱动栈 | 4.5 core | 4.6 core |
|---|---|---|
llvmpipe（默认） | CREATED | **CREATE FAILED** |
Zink + lavapipe | CREATE FAILED | CREATE FAILED |
Zink + kopper | CREATE FAILED | CREATE FAILED |

lavapipe 本身正常（Vulkan 枚举到 CPU 设备），但 Mesa 25.2.8 的 Zink 报
`ZINK: failed to choose pdev` 并拒绝加载，即使不限制 ICD 也一样。

**门禁未被放宽**：声明 `GPU_SKINNING_NO_GL46=1` 是 opt-in，不设时拿不到后端依旧
失败（fixture 直接 `FAIL()`）。Linux 侧已撤掉该声明，Windows 侧保留，理由见 §7。
详见 `docs/Handoff-GLAD-Loader-Minimum-Context.md`（其中测得 GL 4.5 才是引擎真实的
下限，4.6 是唯一超过实际调用需求的版本）。Windows 侧的同类情况见 §5。