# 交接：CI 全量套件的剩余失败

**最新证据**：run `38999782728`（`481d202`）。

| job | 结果 |
|---|---|
| `ubuntu-latest` Release gcc | **13/13 通过** |
| `ubuntu-latest` Release clang | **13/13 通过** |
| `windows-latest` Release cl | 12/13（`test_renderer`，见 §5） |
| `windows-latest` RelWithDebInfo cl **ASan=ON** | 停在「Generate scene fixtures」，见 §4 |

下文 §1–§3 是本文最初的 3 项 Linux 失败，现已全部关闭，保留原文以说明各自的真实根因；
§4 是当前仍未关闭的一项；§5 已按方案 1 关闭，但保留代价说明与退路。

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

## GPU 像素验证的独立限制（已在 CI 中显式记录）

4 个 `GPUSkinningPixelTest` 用例在 Linux runner 上**不会真正执行像素验证**，CI 会输出：

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

**门禁未被放宽**：声明 `GPU_SKINNING_NO_GL46=1` 是 opt-in，不设时拿不到上下文依旧失败。
详见 `docs/Handoff-GLAD-Loader-Minimum-Context.md`（其中测得 GL 4.5 才是引擎真实的
下限，4.6 是唯一超过实际调用需求的版本）。Windows 侧的同类情况见 §5。