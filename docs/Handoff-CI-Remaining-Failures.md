# 交接：Linux CI 全量套件剩余 3 项失败

**状态**：Linux Release 构建已**首次完整通过**（68 个 target，`all` 阶段成功）。
13 个 CTest 用例中 10 个通过，3 个失败。本文只记录这 3 项失败，均为**既有**问题，
不是引入的回归。

最近一次证据：run `38292047931`（gcc）—— `77% tests passed, 3 tests failed out of 13`。

---

## 1. `test_gp01` —— 引用从未入库的资产（段错误）

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

### 一个已排除的假设

曾怀疑是「ctest 工作目录是构建输出目录，仓库相对路径解析不到」。**该假设错误**：
`ctest` 的 working-directory 确实是构建目录，但 `GL46DeviceTest.cpp::ResolveShaderPath`
早就为此做了向上遍历定位仓库根的处理；真正原因是文件本身不在仓库里。
曾按该假设改写 13 处调用点并新增 `tests/utils/TestAssets.h`，实测无效后已整体回滚
（`63f1725`），当前树与 `a061ff8` 完全一致。

### 需要 owner 做什么

确认 `Main.scene` 是遗漏提交，还是 `GP01Harness.h` 应当指向另一个已入库的场景文件。

---

## 2. `test_content` —— 同类资产缺失（非段错误）

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

### 需要 owner 做什么

确认这些场景资产是遗漏提交，还是这四组用例应对齐到已入库的资产。

---

## 3. `test_bridge` —— 属 HRC 所有权范围

`bridge/` 与 `tests/test_bridge/test_editor_capi.cpp` 均为 **HRC-OWNED**（主工作树中
存在未提交改动），本次工作全程未触碰。段错误发生在
`tests/test_bridge/test_editor_capi.cpp` 的 CAPI 桥接用例（至少 9 处断言失败后崩溃）。

### 需要 owner 做什么

由 HRC owner 定位 CAPI 桥接在 Linux 上的崩溃原因。

---

## 本次已修复（供对照，确认无回归）

| 测试 | 修复前 | 修复后 |
|---|---|---|
`test_renderer` | 失败 | 通过（4 个 GPU 用例显式 SKIP） |
`test_io` | 失败 | 通过 |
`test_physics` | 失败 | 通过 |
`test_content` | **段错误** | 普通断言失败 |
Linux `all` 构建 | 45 个 target 中止 | 68 个 target **通过** |

另：Linux 套件整体从 **54%（6 失败）** 提升到 **77%（3 失败）**。

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
详见 `docs/Handoff-GLAD-Loader-Minimum-Context.md`。